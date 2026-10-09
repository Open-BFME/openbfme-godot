// OpenBFME. GPL-3.0.
//
// W3DLuaDrawScriptHost: the real Lua behind W3DDrawScriptHost (lane LUA-1; replaces the interim hand written subsets of stop S-091:
// tests/MiniDrawLua.h and w3dminilua::MiniDrawLuaHost). A BeginScript body is compiled and run by EA's Lua fork in the DRAWABLE state of
// the script engine (LuaDrawableState, RW 0x734D62), whose CurDrawable* functions reach the draw module through W3DDrawScriptApi.
//
// The drawable state is one per host and keeps its globals between runs and across drawables, as retail's does (the `Prev` of five retail
// scripts is read before it is assigned: spec 1.5). Swap this class in where MiniDrawLuaHost was constructed:
//     W3DLuaDrawScriptHost host;                       // was: w3dminilua::MiniDrawLuaHost host;
//     new W3DScriptedModelDraw(data, assets, random, &host, options);
//
// What W3DDrawScriptApi cannot answer (the draw layer has no such data) is reported through reports() as stop S-126, never invented:
// CurDrawableObjectStatus, the target distance and height, the module-first rule of Show/HideSubObject (no module tags are exposed), and
// GetFrame without a frame source. A script that fails is swallowed like retail (the alert is in failures()) and run() returns false with the
// message, so the draw layer records its error.

#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GameLogic/ScriptEngine/LuaDrawableState.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class W3DLuaDrawScriptHost : public W3DDrawScriptHost
{
public:
	struct Options
	{
		std::function<std::uint32_t()> frame;  ///< GetFrame: the logic frame. Without it GetFrame reports S-124 and answers 0
		std::function<bool()> audioAvailable;  ///< TheAudio != 0; default true
	};
	W3DLuaDrawScriptHost();
	explicit W3DLuaDrawScriptHost(const Options &options);
	~W3DLuaDrawScriptHost() override;

	bool run(const std::string &script, W3DDrawScriptApi &api, std::string *returned, std::string *error) override;

	// the chunk name of the next runs (retail: the drawable's object template name; "" when it has none)
	void setChunkName(const std::string &name) { m_chunkName = name; }

	LuaReportSink &reports() { return m_sink; }
	const LuaReportSink &reports() const { return m_sink; }
	LuaDrawableState &state() { return *m_state; }
	size_t scriptsRun() const { return m_scripts; }
	// one line per failed run: the Lua message and the script text
	const std::vector<std::string> &failures() const { return m_failures; }

private:
	LuaReportSink m_sink;
	std::unique_ptr<LuaDrawableState> m_state;
	std::string m_chunkName;
	size_t m_scripts = 0;
	std::vector<std::string> m_failures;
};
