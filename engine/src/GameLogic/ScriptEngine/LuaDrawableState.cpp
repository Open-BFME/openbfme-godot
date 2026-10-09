// OpenBFME. GPL-3.0. See LuaDrawableState.h (RW 0x737654 init, 0x734D62 run).

#include "GameLogic/ScriptEngine/LuaDrawableState.h"

#include "GameLogic/ScriptEngine/LuaBindings.h"

extern "C"
{
#include "lua.h"
}

LuaDrawableState::LuaDrawableState(LuaReportSink &sink, std::function<std::uint32_t()> frame, std::function<bool()> audioAvailable)
	: m_runtime(new LuaRuntime(sink, "drawable"))
	, m_audio(std::move(audioAvailable))
{
	m_runtime->setOwner(this);
	m_runtime->setFrameSource(std::move(frame));
	LuaRegisterDrawableBindings(m_runtime->state());
}

LuaDrawableState::~LuaDrawableState() = default;

int LuaDrawableState::run(const std::string &script, const std::string &chunkName, LuaDrawableContext *ctx, std::string *result)
{
	result->clear();
	m_context = ctx;
	lua_State *L = m_runtime->state();
	lua_settop(L, 0);
	const int status = m_runtime->doBuffer(script, chunkName.c_str());
	if (status == 0 && lua_gettop(L) > 0)
	{
		if (const char *s = lua_tostring(L, 1)) // RW 0xB5B600: a number or boolean is converted, nil / a table / a function give NULL
		{
			*result = s;
		}
	}
	lua_settop(L, 0);
	m_context = nullptr;
	return status;
}
