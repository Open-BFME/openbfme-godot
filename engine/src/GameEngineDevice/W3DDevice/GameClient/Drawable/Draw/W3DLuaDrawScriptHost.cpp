// OpenBFME. GPL-3.0. See W3DLuaDrawScriptHost.h.

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLuaDrawScriptHost.h"

#include "GameLogic/ScriptEngine/LuaStops.h"

#include <string>

namespace
{
// the script context (RW LuaScriptEngine+0x9C) over one W3DDrawScriptApi
class ApiContext : public LuaDrawableContext
{
public:
	ApiContext(W3DDrawScriptApi &api, LuaReportSink &sink) : m_api(api), m_sink(sink) {}

	std::string previousAnimationState() override { return m_api.prevAnimationState(); }
	std::string previousAnimation() override { return m_api.prevAnimation(); }
	float previousAnimFraction() override { return m_api.prevAnimFraction(); }
	void setTransitionAnimState(const std::string &name) override { m_api.setTransitionAnimState(name); }
	void allowToContinue() override { m_api.allowToContinue(); }
	bool modelCondition(const std::string &name) override { return m_api.modelCondition(name); }
	bool objectStatus(const std::string &) override
	{
		m_sink.report(LUA_STOP_DRAW_CONTEXT, "CurDrawableObjectStatus: W3DDrawScriptApi (lane DRAW-1) does not expose the object's status bits; answered nil");
		return false;
	}
	bool hasModule(const std::string &name) override
	{
		if (m_api.knowsModules())
		{
			return m_api.hasModule(name); // lane BUILD-4: the drawable's draw module tags (RW 0x734EF4 -> 0x6789B4)
		}
		m_sink.report(LUA_STOP_DRAW_CONTEXT, "CurDrawableShowSubObject / HideSubObject try a draw module of that tag first (RW 0x734EF4, 0x6789B4); "
			"W3DDrawScriptApi exposes no module tags, so the name is always taken as a sub object");
		return false;
	}
	bool showModule(const std::string &name, bool visible, bool) override
	{
		if (m_api.knowsModules())
		{
			// lane BUILD-4: the drawable applies it (Drawable::showModule RW 0x6789B4: the module with that tag is drawn / not drawn); retail answers whether
			// a module has the tag
			const bool has = m_api.hasModule(name);
			if (has)
			{
				visible ? m_api.showModule(name) : m_api.hideModule(name);
			}
			return has;
		}
		// the explicit CurDrawableShowModule / HideModule (RW 0x735192 / 0x7351AB via 0x73511D, Drawable::showModule RW 0x6789B4) changes the visibility of
		// the drawable's module with that tag. W3DDrawScriptApi only records the request on this draw module (a set no renderer reads), so the visibility
		// is NOT applied: S-124, reported at every such request. (Retail does nothing when no module has the tag; the api cannot tell.)
		m_sink.report(LUA_STOP_CALLEE, std::string(visible ? "CurDrawableShowModule" : "CurDrawableHideModule") + " (RW 0x73511D -> Drawable::showModule 0x6789B4) '" + name +
			"': the module's visibility is recorded on the draw module but not applied to any drawn model");
		if (visible)
		{
			m_api.showModule(name);
		}
		else
		{
			m_api.hideModule(name);
		}
		return true;
	}
	void showSubObject(const std::string &name, bool visible, bool permanent) override
	{
		if (permanent)
		{
			visible ? m_api.showSubObjectPermanently(name) : m_api.hideSubObjectPermanently(name);
		}
		else
		{
			visible ? m_api.showSubObject(name) : m_api.hideSubObject(name);
		}
	}
	void playSound(const std::string &name) override
	{
		// RW 0x73529F plays the sound through TheAudio for the current drawable. AUDIO-2: a draw module whose drawable gave it the audio service plays it
		// (AudioApi::playSoundForDrawable); without one the request is only logged by the draw module and reported
		if (!m_api.playsSound())
		{
			m_sink.report(LUA_STOP_CALLEE, "CurDrawablePlaySound (RW 0x73529F) '" + name + "': there is no audio service, the sound is logged on the draw module and not played");
		}
		m_api.playSound(name);
	}
	bool targetDistance(double *) override
	{
		m_sink.report(LUA_STOP_DRAW_CONTEXT, "CurDrawableGetCurrentTargetDistance / Height: W3DDrawScriptApi does not expose the target position; answered nil");
		return false;
	}
	bool targetHeight(double *) override
	{
		m_sink.report(LUA_STOP_DRAW_CONTEXT, "CurDrawableGetCurrentTargetDistance / Height: W3DDrawScriptApi does not expose the target position; answered nil");
		return false;
	}
	bool targetBearing(double *out) override
	{
		*out = (double)m_api.currentTargetBearing();
		return true;
	}
	int targetKindOf(const std::string &kind) override { return m_api.isCurrentTargetKindOf(kind) ? 2 : 1; }
	float clientRandomReal(float lo, float hi) override { return m_api.clientRandomReal(lo, hi); }

private:
	W3DDrawScriptApi &m_api;
	LuaReportSink &m_sink;
};
} // namespace

W3DLuaDrawScriptHost::W3DLuaDrawScriptHost() : W3DLuaDrawScriptHost(Options()) {}

W3DLuaDrawScriptHost::W3DLuaDrawScriptHost(const Options &options)
	: m_state(new LuaDrawableState(m_sink, options.frame, options.audioAvailable))
{
}

W3DLuaDrawScriptHost::~W3DLuaDrawScriptHost() = default;

bool W3DLuaDrawScriptHost::run(const std::string &script, W3DDrawScriptApi &api, std::string *returned, std::string *error)
{
	++m_scripts;
	ApiContext ctx(api, m_sink);
	LuaRuntime &rt = m_state->runtime();
	const size_t alertsBefore = rt.alerts().size();
	const int status = m_state->run(script, m_chunkName, &ctx, returned);
	if (status == 0)
	{
		return true;
	}
	// the error went to _ALERT and was swallowed, as in retail; the draw layer is told so it can record it
	std::string message = "Lua status " + std::to_string(status);
	if (rt.alerts().size() > alertsBefore)
	{
		message = rt.alerts().back();
	}
	m_failures.push_back(message + "  in: " + script);
	*error = message;
	return false;
}
