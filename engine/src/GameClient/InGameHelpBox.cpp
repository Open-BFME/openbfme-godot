// OpenBFME. GPL-3.0.
// See InGameHelpBox.h (lane HUD-6). Client-only.

#include "GameClient/InGameHelpBox.h"

#include <cstdio>
#include <cstdlib>

void InGameHelpBox::loaded(const std::string &clip, CallFn call)
{
	// RW 0x6D4F28: level = the "_level<N>" of the clip (RW 0x8155D9), name = the clip without it (RW 0x815563); RW 0x92E689 clears the state (0, -1 width)
	int level = 0;
	std::string name = clip;
	if (clip.rfind("_level", 0) == 0)
	{
		const size_t dot = clip.find('.');
		level = std::atoi(clip.substr(6, dot == std::string::npos ? std::string::npos : dot - 6).c_str());
		name = dot == std::string::npos ? std::string() : clip.substr(dot + 1);
	}
	m_call = std::move(call);
	m_loaded = true;
	m_name = name;
	char buf[32];
	std::snprintf(buf, sizeof(buf), "_level%d.", level); // RW 0x92E588 (string RW 0xC7C4A4)
	m_renderName = std::string(buf) + name + "_Content";  // RW 0x92E6DC (string RW 0xC7EFF0)
	m_state = 0;
	m_countdown = 0;
	m_width = -1;
	m_help.reset();
}

void InGameHelpBox::unloaded()
{
	m_loaded = false;
	m_call = nullptr;
	m_help.reset();
	m_state = 0;
}

bool InGameHelpBox::call(const std::string &fn, const std::vector<std::string> &args)
{
	std::string line = fn + "(";
	for (size_t i = 0; i < args.size(); ++i)
	{
		line += (i ? "," : "") + args[i];
	}
	line += ")";
	if (m_calls.size() >= 64)
	{
		m_calls.erase(m_calls.begin());
	}
	m_calls.push_back(line);
	return m_call && m_call(m_name, fn, args);
}

void InGameHelpBox::setHelp(std::shared_ptr<CommandButtonHelp> help)
{
	// RW 0x92E570 -> 0x92E362: a shown box hides (state 2) and the help goes, then the new one is kept
	if (m_state == 3 || m_state == 4)
	{
		call("Hide", {});
		m_state = 2;
	}
	m_help = std::move(help);
}

void InGameHelpBox::hideSoon()
{
	if (m_state == 3) // RW 0x92E229
	{
		m_countdown = 5;
		m_state = 4;
	}
}

void InGameHelpBox::update(const std::function<int(CommandButtonHelp &, int)> &measure, float stageScaleY)
{
	if (!m_loaded)
	{
		return;
	}
	switch (m_state) // RW 0x92E462
	{
		case 0:
			call("SampleContentWidth", {});
			m_width = -1;
			m_state = 1;
			break;
		case 1:
			if (m_width > 0)
			{
				call("Hide", {});
				m_state = 2;
			}
			break;
		case 2:
			if (m_help)
			{
				const int height = measure(*m_help, m_width);
				char buf[64];
				std::snprintf(buf, sizeof(buf), "%f", (double)((float)height * stageScaleY)); // RW 0x92E3AA: the float as "%f" (RW 0x6228E8)
				call("Show", { buf });
				m_state = 3;
			}
			break;
		case 4:
			if (--m_countdown <= 0)
			{
				// RW 0x92E362: Hide, state 2, the help released
				call("Hide", {});
				m_state = 2;
				m_help.reset();
			}
			break;
		default:
			break;
	}
}

void InGameHelpBox::render(float x, float y, float w, float h, std::vector<HelpDrawOp> &out)
{
	if (m_state == 1)
	{
		const int sampled = (int)(w + 0.5f); // RW 0x92E2B9: cvttss2si(width + 0.5f), at least 1
		m_width = sampled < 1 ? 1 : sampled;
	}
	else if ((m_state == 3 || m_state == 4) && m_help)
	{
		m_help->render(x, y, w, h, out);
	}
}

void InGameHelpBox::beginFrame()
{
	// ControlBar::update RW 0x71FC08: no hover marked in the last frame -> the box hides soon (when shown), the provider goes
	if (!m_marked)
	{
		if (m_loaded && isShown())
		{
			hideSoon();
		}
		m_haveProvider = false;
	}
	m_marked = false;
}

// lane OWNERFIX-1: RW 0x8076CD (InGameHelpBox.h)
void InGameHelpBox::dropProvider()
{
	m_marked = false;
	m_haveProvider = false;
	m_shownForProvider = false;
	if (m_loaded && isShown())
	{
		hideSoon(); // RW 0x6D473D -> the help box's vslot 1 (RW 0x92E229): a shown help counts down to its hide
	}
}

void InGameHelpBox::showHelp(const Provider &p, std::uint32_t nowMs, int tooltipDelayMs, const std::function<std::shared_ptr<CommandButtonHelp>()> &compose)
{
	if (m_tooltipsDisabled && m_tooltipsDisabled()) // RW 0x807861: InGameUI vslot +0x1BC, the first guard
	{
		return;
	}
	if (m_endGameTimer >= 0) // RW 0x80786B: ScriptEngine + 0x1A204, including the expired timer (zero).
	{
		return;
	}
	if (!m_hoverStartSet) // RW 0x807886: the static's first use
	{
		m_hoverStartSet = true;
		m_hoverStart = nowMs;
	}
	if (m_haveProvider && m_provider == p)
	{
		m_marked = true;
		if (m_shownForProvider)
		{
			return;
		}
		if (nowMs <= m_hoverStart + (std::uint32_t)tooltipDelayMs) // RW 0x8078E0: unsigned
		{
			return;
		}
		m_shownForProvider = true;
		if (m_modeGate810 && m_modeGate810()) // RW 0x807978: consumed first, then InGameUI vslot +0x17C (+0x810)
		{
			return;
		}
		if (m_disconnectMenuPresent) // RW 0x807982: consumed even though composition is suppressed.
		{
			return;
		}
		std::shared_ptr<CommandButtonHelp> help = compose(); // RW 0x807676: the provider's vslot 3, then the Palantir's help box
		if (help && m_loaded)
		{
			setHelp(help);
			++m_helpsShown;
		}
		return;
	}
	if (!m_loaded || isShown())
	{
		m_haveProvider = false; // RW 0xA0CB16: release the old provider before requesting the hide.
		m_provider = Provider();
		if (m_loaded)
		{
			hideSoon();
		}
		return;
	}
	m_provider = p; // RW 0x807914 .. 0x80795B
	m_haveProvider = true;
	m_marked = true;
	m_hoverStart = nowMs;
	m_shownForProvider = false;
}

std::vector<std::string> InGameHelpBox::acceptanceStops()
{
	return {
		"[S-2703] help box (HUD-6): ported the help box movie clip (RW 0x92E462 / 0x92E2AC / 0x92E570 / 0x92E229), the control bar's provider (RW 0x807848, "
		"ControlBar::update RW 0x71FC08); INFERENCE: the hovered Palantir command button is the Apt player's current button inside a content the control bar "
		"filled (the content updater's rollover route, RW 0xC8C3D4 vslot 2, was not traced to its caller), the radial bubbles' hover is the ring's hit test, "
		"the Apt player's y scale is the stage height over the window height; ported (lane ROTWK-C): showHelp's end-game timer guard (RW 0xDE3BAC + "
		"0x1A204), the provider release RW 0xA0CB16 and the late TheDisconnectMenu guard (RW 0xDEA328); ported (lane HOVER-1): showHelp's InGameUI guards "
		"vslot 0x1BC (areTooltipsDisabled, first) and vslot 0x17C (+0x810, after the hover is consumed), and the hero bar's and the spell book's providers "
		"(S-4612); not ported: the +0x810 owner/callers, the setting behind byteDD0 and the real Living World query (a hook); a UnitHelpSource's "
		"delay (+0x08, RW 0x9F9F16) is ported as zero (RW 0x9309DF / 0x92BFBB zero the stack provider's field)"
	};
}
