// OpenBFME. GPL-3.0.
//
// LuaDrawableState: the DRAWABLE Lua state of the retail LuaScriptEngine (RW LuaScriptEngine+0x10, created by init() 0x737654; spec
// lua-scripting.md 2.2, 3.3, 4.7) on its own, so the draw layer can run BeginScript bodies without the logic state, the event registry or the
// object table. It owns a LuaRuntime with the 21 CurDrawable* / GetClientRandomNumberReal / GetFrame / _ALERT functions registered in the
// retail order. LuaScriptEngine holds one.
//
// run() is RW 0x734D62: the context becomes current, the stack is emptied, the body is compiled and run with the chunk name given (retail: the
// drawable's object template name), and when it succeeded and left a value, that value as a string is the result (the Animation label); the
// stack is emptied again and the context cleared. Nothing is cached, the state's globals persist between runs and across drawables.

#pragma once

#include "GameLogic/ScriptEngine/LuaHost.h"
#include "GameLogic/ScriptEngine/LuaRuntime.h"

#include <functional>
#include <memory>
#include <string>

class LuaDrawableState
{
public:
	// `frame`: GetFrame; `audioAvailable`: TheAudio != 0 (CurDrawablePlaySound does nothing without it)
	LuaDrawableState(LuaReportSink &sink, std::function<std::uint32_t()> frame, std::function<bool()> audioAvailable);
	~LuaDrawableState();

	// the Lua status (0 = ok); *result is "" when the script returned nothing or failed. A failure is swallowed like retail's (alerts())
	int run(const std::string &script, const std::string &chunkName, LuaDrawableContext *ctx, std::string *result);

	LuaRuntime &runtime() { return *m_runtime; }
	LuaDrawableContext *context() const { return m_context; } ///< LuaScriptEngine+0x9C
	bool audioAvailable() const { return m_audio ? m_audio() : true; }

private:
	std::unique_ptr<LuaRuntime> m_runtime;
	LuaDrawableContext *m_context = nullptr;
	std::function<bool()> m_audio;
};
