// OpenBFME. GPL-3.0.
//
// LuaRuntime: one Lua state opened the way the retail LuaScriptEngine opens its two (spec lua-scripting.md 2.2, 2.4): lua_open(0x100)
// (RW 0xB62250), the five libraries in the order of openLuaLibraries (RW 0x734478: base 0xB60B90, io 0xB5F820, str 0xB5E300, math
// 0xB5CCA0, db 0xB5C5D0) and the line hook (RW 0x735F1C, inert unless a debug step is requested). The library is EA's fork of Lua 4.0.1
// (engine/src/Libraries/Lua/README.md).
//
// Retail swallows Lua errors: `_ERRORMESSAGE` (the io library's errorfb) builds a traceback and calls `_ALERT`, which builds "LUA Alert: " +
// message and drops it (RW 0x734E41). The port keeps the message in alerts() (a diagnostic, not a behaviour: nothing in the script's world
// changes). print (RW 0xB5F950) writes through the game's logger only when an options byte is set (RW 0xDE4364 -> +0x9C1, stop S-125:
// its owner is unidentified), so printed() stays empty unless setPrintEnabled(true).

#pragma once

#include "GameLogic/ScriptEngine/LuaHost.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct lua_State;
struct lua_EA_Host;

class LuaRuntime
{
public:
	LuaRuntime(LuaReportSink &sink, const std::string &name);
	~LuaRuntime();
	LuaRuntime(const LuaRuntime &) = delete;
	LuaRuntime &operator=(const LuaRuntime &) = delete;

	lua_State *state() const { return m_L; }
	const std::string &name() const { return m_name; }
	LuaReportSink &sink() const { return m_sink; }

	// the swallowed _ALERT messages, oldest first (the retail text is "LUA Alert: " + message)
	const std::vector<std::string> &alerts() const { return m_alerts; }
	void recordAlert(const std::string &text) { m_alerts.push_back(text); }
	// Compatibility faults: places where retail faults (an access violation) and the retail-compatible profile aborts the running chunk instead
	// (S-120 / S-041). Each is also reported. A non-zero count means a script did NOT complete compatibly.
	const std::vector<std::string> &faults() const { return m_faults; }
	void recordFault(const std::string &text) { m_faults.push_back(text); }
	// LUA_EA_PROFILE_ENHANCED answers the repaired behaviour instead of faulting; explicit, never a default
	void setEnhancedProfile(bool enhanced);
	void clearAlerts() { m_alerts.clear(); }
	// what print() sent to the logger while the gate (retail: an options byte) was open
	const std::string &printed() const { return m_printed; }
	void clearPrinted() { m_printed.clear(); }
	void appendPrinted(const char *text) { m_printed += text; }
	void setPrintEnabled(bool on) { m_printEnabled = on; }
	bool printEnabled() const { return m_printEnabled; }

	// the logic frame GetFrame reports in this state (RW 0x73461B reads TheGameLogic); without a source GetFrame reports the stop S-124
	void setFrameSource(std::function<std::uint32_t()> source) { m_frame = std::move(source); }
	std::uint32_t frame();

	// the owner (LuaScriptEngine or LuaDrawableState) the C bindings find through the state
	void setOwner(void *owner) { m_owner = owner; }
	void *owner() const { return m_owner; }
	static LuaRuntime *of(lua_State *L); ///< the runtime a state belongs to

	// lua_dobuffer with a chunk name (RW 0xB61420); returns the Lua status (0 = ok), the stack is left as the call left it
	int doBuffer(const std::string &text, const char *chunkName);

private:
	lua_State *m_L = nullptr;
	LuaReportSink &m_sink;
	std::string m_name;
	std::vector<std::string> m_alerts;
	std::vector<std::string> m_faults;
	std::string m_printed;
	bool m_printEnabled = false;
	void *m_owner = nullptr;
	std::function<std::uint32_t()> m_frame;
	struct HostTable;
	HostTable *m_host = nullptr;
};
