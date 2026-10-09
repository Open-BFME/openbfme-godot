// OpenBFME. GPL-3.0.
//
// The acceptance stops of lane LUA-1 (PLAN "Acceptance stops"; docs/STOPS.md has the rows, S-120..S-129). A stop is reported at runtime
// through LuaReportSink, pinned by a test and registered; the rest of the lane works around it.

#pragma once

// The Lua library is a reconstruction of EA's fork (no EA source exists): stock 4.0.1 plus the patch layer, verified against the retail
// disassembly function by function and against the values recorded from retail by tools/retail_oracle; the places where retail faults
// (type(<boolean>), S-041) and the choices made there.
#define LUA_STOP_FORK "S-120"
// callees whose retail identity or exact behaviour is inferred (spec G2, G3, G9, G10, G16): reported each time such a binding runs
#define LUA_STOP_INFERRED "S-121"
// the host-affecting library functions (files, processes, clock, locale, environment, dofile, io.debug) are refused and reported
#define LUA_STOP_HOST_REFUSAL "S-122"
// numerics: PC24 assumed while scripts run; CRT math, formatting beyond tostring, strtod subnormals
#define LUA_STOP_NUMERICS "S-123"
// an engine callee a binding needs is not implemented (LuaGameHost default): the call is reported, not performed
#define LUA_STOP_CALLEE "S-124"
// print is gated by an options byte whose owner is unidentified: closed by default
#define LUA_STOP_PRINT_GATE "S-125"
// the drawable state's context: capabilities the draw layer does not provide (module-first show/hide, object status, target distance/height)
#define LUA_STOP_DRAW_CONTEXT "S-126"
// loading: a missing Scripts.lua / ScriptEvents.xml, the map overlay path, the "<?" declaration, the XML parse paths that retail only logs, duplicate lists
#define LUA_STOP_LOAD "S-127"
// ObjectSpy's re-dispatch and the debug console are not ported (unused by retail data)
#define LUA_STOP_SPY "S-128"
// the event sources of the owning lanes (OnCreated, OnDamaged, ...) and the events nothing fires here
#define LUA_STOP_SOURCES "S-129"
