// OpenBFME. GPL-3.0. See LuaRuntime.h.

#include "GameLogic/ScriptEngine/LuaRuntime.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lua_ea.h"
#include "luadebug.h"
#include "lualib.h"
}

#include "GameLogic/ScriptEngine/LuaStops.h"

#include <stdexcept>

// ---------------------------------------------------------------------------------------------------------------------
// LuaReportSink
// ---------------------------------------------------------------------------------------------------------------------
void LuaReportSink::report(const std::string &stop, const std::string &text)
{
	for (Entry &e : m_entries)
	{
		if (e.stop == stop && e.text == text)
		{
			++e.count;
			return;
		}
	}
	Entry e;
	e.stop = stop;
	e.text = text;
	e.count = 1;
	m_entries.push_back(e);
}

size_t LuaReportSink::count(const std::string &stop) const
{
	size_t n = 0;
	for (const Entry &e : m_entries)
	{
		if (e.stop == stop)
		{
			n += e.count;
		}
	}
	return n;
}

bool LuaReportSink::has(const std::string &stop, const std::string &textPart) const
{
	for (const Entry &e : m_entries)
	{
		if (e.stop == stop && e.text.find(textPart) != std::string::npos)
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// LuaRuntime
// ---------------------------------------------------------------------------------------------------------------------
struct LuaRuntime::HostTable
{
	lua_EA_Host host;
};

namespace
{
// RW 0x735F1C: nothing unless a debug step is active (global 0xDE7808 == L), and none ever is
void lineHook(lua_State *, lua_Debug *) {}

void hostLog(void *user, const char *text)
{
	LuaRuntime *rt = static_cast<LuaRuntime *>(user);
	if (rt->printEnabled())
	{
		rt->appendPrinted(text);
	}
	else
	{
		// RW 0x7355A6 writes only when an options byte (*(0xDE4364)+0x9C1) is set; its owner is unidentified (spec G6): closed by default
		rt->sink().report(LUA_STOP_PRINT_GATE, "[" + rt->name() + " state] print output was dropped: the retail logger writes only when an options byte (RW 0xDE4364+0x9C1) is set; "
			"its owner is not identified, so the gate is closed (LuaRuntime::setPrintEnabled opens it)");
	}
}

void hostAlert(void *user, const char *message)
{
	LuaRuntime *rt = static_cast<LuaRuntime *>(user);
	// RW 0x734E41: AsciiString s("LUA Alert: "); if (message) s.concat(message); then released. The message is kept for the tests
	rt->recordAlert(std::string("LUA Alert: ") + (message ? message : ""));
}

void hostFault(void *user, const char *stop, const char *message)
{
	LuaRuntime *rt = static_cast<LuaRuntime *>(user);
	rt->recordFault(message);
	rt->sink().report(stop, std::string("[") + rt->name() + " state] COMPATIBILITY FAULT (the script was aborted): " + message);
}

void hostReport(void *user, const char *stop, const char *message)
{
	LuaRuntime *rt = static_cast<LuaRuntime *>(user);
	rt->sink().report(stop, std::string("[") + rt->name() + " state] " + message);
}
} // namespace

LuaRuntime::LuaRuntime(LuaReportSink &sink, const std::string &name)
	: m_sink(sink)
	, m_name(name)
{
	m_L = lua_open(0x100); // RW 0xB62250(0x100)
	if (!m_L)
	{
		throw std::runtime_error("lua_open failed");
	}
	m_host = new HostTable();
	m_host->host.user = this;
	m_host->host.log = &hostLog;
	m_host->host.alert = &hostAlert;
	m_host->host.report = &hostReport;
	m_host->host.fault = &hostFault;
	lua_ea_sethost(m_L, &m_host->host);
	// openLuaLibraries, RW 0x734478
	lua_baselibopen(m_L);
	lua_iolibopen(m_L);
	lua_strlibopen(m_L);
	lua_mathlibopen(m_L);
	lua_dblibopen(m_L);
	lua_setlinehook(m_L, &lineHook);
	// standing provenance reports of every state (PLAN "Acceptance stops")
	m_sink.report(LUA_STOP_FORK, "[" + m_name + " state] the Lua library is a reconstruction of EA's fork: stock Lua 4.0.1 plus a patch layer verified against the RotWK "
		"disassembly and the values recorded from retail (engine/src/Libraries/Lua/README.md); not every opcode has an executed oracle");
	m_sink.report(LUA_STOP_NUMERICS, "[" + m_name + " state] script arithmetic is rounded at x87 precision control 24 (setFPMode RW 0x440809): that the game runs at PC24 while scripts "
		"execute is assumed, not traced. Unresolved against MSVCR71 (no executed golden values): the number text (%.16g is MSVCR71's two stage conversion, 17 digits then 16, each rounded "
		"up by its guard digit alone, taken from the DLL's instructions in an x86 emulator: it agrees with them on 72,590 sampled doubles, e.g. 1000000000000002.5 prints "
		"1000000000000003, and differs from the correctly rounded text; no running Windows game oracle, and the 80 bit power-of-ten table of $I10_OUTPUT is assumed to equal the exact expansion), strtod rounding of long inputs and subnormals, the CRT math library (sin, cos, pow, fmod, ...: the host's) "
		"and string.format's own conversions");
}

LuaRuntime::~LuaRuntime()
{
	if (m_L)
	{
		lua_close(m_L);
	}
	delete m_host;
}

std::uint32_t LuaRuntime::frame()
{
	if (!m_frame)
	{
		m_sink.report("S-124", "engine callee not implemented: TheGameLogic frame counter (RW 0xDE412C+0x40, GetFrame RW 0x73461B) in the " + m_name + " state: no frame source was given");
		return 0;
	}
	return m_frame();
}

LuaRuntime *LuaRuntime::of(lua_State *L)
{
	const lua_EA_Host *h = lua_ea_gethost(L);
	return h ? static_cast<LuaRuntime *>(h->user) : nullptr;
}

void LuaRuntime::setEnhancedProfile(bool enhanced)
{
	lua_ea_setprofile(m_L, enhanced ? LUA_EA_PROFILE_ENHANCED : LUA_EA_PROFILE_RETAIL);
}

int LuaRuntime::doBuffer(const std::string &text, const char *chunkName)
{
	return lua_dobuffer(m_L, text.data(), text.size(), chunkName);
}
