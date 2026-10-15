// OpenBFME. GPL-3.0.
// See CursorTooltip.h (lane HOVER-1). Client-only: the menus' tool tips never touch the logic.

#include "GameClient/GUI/CursorTooltip.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "GameClient/AptCanvas.h"
#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GlobalLanguage.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace
{
std::string trimmed(std::string s)
{
	const size_t c = s.find(';');
	if (c != std::string::npos)
	{
		s.erase(c);
	}
	const size_t slashes = s.find("//");
	if (slashes != std::string::npos)
	{
		s.erase(slashes);
	}
	const size_t b = s.find_first_not_of(" \t\r");
	if (b == std::string::npos)
	{
		return std::string();
	}
	s = s.substr(b);
	while (!s.empty() && std::isspace((unsigned char)s.back()))
	{
		s.pop_back();
	}
	return s;
}
std::vector<std::string> tokens(const std::string &s)
{
	std::vector<std::string> out;
	std::istringstream in(s);
	std::string t;
	while (in >> t)
	{
		out.push_back(t);
	}
	return out;
}
bool lowerEquals(const std::string &a, const char *b)
{
	std::string x = a, y = b;
	std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	std::transform(y.begin(), y.end(), y.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return x == y;
}
// "%g" as MSVCR71's _snprintf prints it (6 significant digits)
std::string printG(double v)
{
	char buf[32];
	std::snprintf(buf, sizeof buf, "%g", v);
	return buf;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// the settings: Mouse.ini's "Mouse" block (one per file) and Language.ini through GlobalLanguage (its RW 0xBF4C20 table)
// ---------------------------------------------------------------------------------------------------------------------------------
bool CursorTooltipSettings::scan(const std::string &mouseIni, const std::string &languageIni, CursorTooltipSettings &out, std::string *error)
{
	CursorTooltipSettings s;
	std::istringstream in(mouseIni);
	std::string raw;
	bool inMouse = false;
	unsigned seen = 0;
	while (std::getline(in, raw))
	{
		const std::string line = trimmed(raw);
		if (line.empty())
		{
			continue;
		}
		if (!inMouse)
		{
			inMouse = lowerEquals(line, "Mouse");
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			if (lowerEquals(tokens(line).front(), "End"))
			{
				break;
			}
			continue;
		}
		const std::string key = trimmed(line.substr(0, eq));
		const std::vector<std::string> v = tokens(line.substr(eq + 1));
		if (v.empty())
		{
			continue;
		}
		if (key == "TooltipFontName") // parseAsciiString RW 0x42EE5E: the first token
		{
			s.font.name = v.front();
			seen |= 1;
		}
		else if (key == "TooltipFontSize")
		{
			s.font.pointSize = std::atoi(v.front().c_str());
			seen |= 2;
		}
		else if (key == "TooltipFontIsBold") // parseBool RW 0x42E558
		{
			s.font.bold = lowerEquals(v.front(), "yes") || lowerEquals(v.front(), "true");
			seen |= 4;
		}
		else if (key == "TooltipDelayTime")
		{
			s.delayMs = std::atoi(v.front().c_str());
			seen |= 8;
		}
		else if (key == "TooltipTextColor") // R:n G:n B:n A:n
		{
			int c[4] = { 0, 0, 0, 255 };
			for (const std::string &t : v)
			{
				const char k = (char)std::toupper((unsigned char)t[0]);
				const int idx = k == 'R' ? 0 : k == 'G' ? 1 : k == 'B' ? 2 : k == 'A' ? 3 : -1;
				const size_t colon = t.find(':');
				if (idx >= 0 && colon != std::string::npos)
				{
					c[idx] = std::atoi(t.c_str() + colon + 1);
				}
			}
			s.textColor = ((std::uint32_t)(c[3] & 0xFF) << 24) | ((std::uint32_t)(c[0] & 0xFF) << 16) | ((std::uint32_t)(c[1] & 0xFF) << 8) | (std::uint32_t)(c[2] & 0xFF);
			seen |= 16;
		}
	}
	if (seen != 31)
	{
		if (error)
		{
			*error = !inMouse ? "mouse.ini: no Mouse block" : "mouse.ini: the Mouse block lacks a Tooltip field (mask " + std::to_string(seen) + ")";
		}
		return false;
	}
	// Language.ini's TooltipFontName (a font description, RW 0x778C93) wins when it names a font (RW 0x5EE8C8)
	if (!languageIni.empty())
	{
		INIEnvironment env;
		INIBlockRecorder recorder;
		GlobalLanguage language;
		RegisterRecordingBlockStubs(env.blocks, recorder, { "Language" }, StubExtent::Lenient);
		language.registerBlocks(env.blocks);
		INI ini(env);
		try
		{
			ini.loadMemory("language.ini", std::vector<std::uint8_t>(languageIni.begin(), languageIni.end()), INI_LOAD_OVERWRITE);
		}
		catch (const std::exception &e)
		{
			if (error)
			{
				*error = std::string("language.ini: ") + e.what();
			}
			return false;
		}
		if (!language.tooltipFontName.name.empty())
		{
			s.font.name = language.tooltipFontName.name;
			s.font.pointSize = language.tooltipFontName.size;
			s.font.bold = language.tooltipFontName.bold;
		}
	}
	s.loaded = true;
	out = s;
	return true;
}

bool CursorTooltipSettings::load(ArchiveFileSystem &fs, CursorTooltipSettings &out, std::string *error)
{
	std::vector<std::uint8_t> mouse, language;
	if (!fs.readFile("data/ini/mouse.ini", mouse, error))
	{
		return false;
	}
	if (!fs.readFile("language.ini", language, error))
	{
		return false;
	}
	return scan(std::string(mouse.begin(), mouse.end()), std::string(language.begin(), language.end()), out, error);
}

bool AptButtonTooltipMap::scan(const std::string &text, AptButtonTooltipMap &out, std::string *error)
{
	AptButtonTooltipMap m;
	std::istringstream in(text);
	std::string raw;
	bool inBlock = false, sawBlock = false;
	while (std::getline(in, raw))
	{
		const std::string line = trimmed(raw);
		if (line.empty())
		{
			continue;
		}
		if (!inBlock)
		{
			inBlock = sawBlock = lowerEquals(tokens(line).front(), "AptButtonTooltipMap");
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			if (lowerEquals(tokens(line).front(), "End"))
			{
				inBlock = false;
			}
			continue;
		}
		if (trimmed(line.substr(0, eq)) != "ButtonMap")
		{
			continue;
		}
		const std::vector<std::string> v = tokens(line.substr(eq + 1));
		if (v.size() != 2)
		{
			if (error)
			{
				*error = "aptbuttontooltipmap.ini: a ButtonMap row needs a path and a name: " + line;
			}
			return false;
		}
		m.buttonMap[v[0]] = v[1]; // a later row of the same path replaces the earlier (the retail data repeats one identically)
	}
	if (!sawBlock)
	{
		if (error)
		{
			*error = "aptbuttontooltipmap.ini: no AptButtonTooltipMap block";
		}
		return false;
	}
	out = m;
	return true;
}

bool AptButtonTooltipMap::load(ArchiveFileSystem &fs, AptButtonTooltipMap &out, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data/ini/aptbuttontooltipmap.ini", bytes, error))
	{
		return false;
	}
	return scan(std::string(bytes.begin(), bytes.end()), out, error);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// CursorTooltip
// ---------------------------------------------------------------------------------------------------------------------------------
void CursorTooltip::setTooltip(const UnicodeString &text, int delayMs)
{
	m_delayOverride = delayMs; // RW 0x5EE1A6: + 0x4FCC, the request + 0x12FC (the colour: the default, no colour is passed)
	m_request = text;
}

void CursorTooltip::update(std::uint32_t nowMs, float stageX, float stageY, const CursorTooltipSettings &settings, float scaleX, float scaleY,
                           FontMetricsSource &metrics, std::vector<Call> &out)
{
	if (m_request != m_mirror) // a new request restarts the clock
	{
		m_mirror = m_request;
		m_changedMs = nowMs;
	}
	const int delay = m_delayOverride >= 0 ? m_delayOverride : settings.delayMs; // GlobalData + 0x9C1 is false
	m_delayPassed = nowMs - m_changedMs >= (std::uint32_t)delay;
	if (m_delayPassed && !m_request.empty()) // GlobalData + 0x9BF is true, a display exists
	{
		if (m_sent != m_request)
		{
			m_sent = m_request;
			// ShowToolTip (RW 0x782DD9): an earlier string is hidden first
			if (m_haveString)
			{
				out.push_back({ "HideToolTip", {} });
			}
			// the string at the tool tip font's size times min(the window's x / y scale) in window pixels (RW 0x782E15 .. 0x782E30). The metrics
			// measure a font at the stage's own size, as the device draws the draw list's texts (scaled by min(scale) once): the pixel sizes are
			// the stage measure times min(scale), so the box the movie draws and the text the device draws agree (INFERENCE, S-4610)
			const float minScale = scaleX < scaleY ? scaleX : scaleY;
			m_font = settings.font;
			m_color = settings.textColor;
			m_lines.clear();
			size_t start = 0;
			for (;;)
			{
				const size_t nl = m_sent.find(u'\n', start);
				m_lines.push_back(m_sent.substr(start, nl == UnicodeString::npos ? UnicodeString::npos : nl - start));
				if (nl == UnicodeString::npos)
				{
					break;
				}
				start = nl + 1;
			}
			int stageW = 0;
			for (const UnicodeString &l : m_lines)
			{
				stageW = std::max(stageW, metrics.textWidth(m_font, l));
			}
			const float lineH = (float)metrics.fontHeight(m_font) * minScale;
			m_textW = (int)((float)stageW * minScale);
			m_textH = (int)(lineH * (float)m_lines.size());
			m_haveString = true;
			const int pad = ((int)lineH + 1) / 3; // (font height + 1) / 3 (RW 0x782EBA .. 0x782EC5)
			out.push_back({ "ShowToolTip",
				{ printG((double)(((float)(m_textW + pad) / scaleX) * 0.5f)), printG((double)((float)(m_textH + pad) / scaleY)) } });
			// MoveToolTip (RW 0x782CDC): the pointer's window position times the stage per pixel: its stage position
			out.push_back({ "MoveToolTip", { printG((double)stageX), printG((double)stageY) } });
		}
		m_hideCount = 4;
		return;
	}
	if (m_sent.empty())
	{
		return;
	}
	if (--m_hideCount > 0)
	{
		return;
	}
	out.push_back({ "HideToolTip", {} }); // RW 0x782CA0: the string goes
	m_haveString = false;
	m_lines.clear();
	m_sent.clear();
}

void CursorTooltip::render(float x, float y, float w, float h, float scaleX, float scaleY, GadgetDrawList &out) const
{
	if (!m_haveString)
	{
		return;
	}
	// RW 0x782D52: centred, trunc(v + 0.5); the string's pixel size taken to stage units (INFERENCE, S-4610)
	const float tw = (float)m_textW / scaleX, th = (float)m_textH / scaleY;
	const float lineH = m_lines.empty() ? 0.0f : th / (float)m_lines.size();
	const int left = (int)(x + w * 0.5f - tw * 0.5f + 0.5f);
	const int top = (int)(y + h * 0.5f - th * 0.5f + 0.5f);
	for (size_t i = 0; i < m_lines.size(); ++i)
	{
		GadgetDrawCommand c;
		c.kind = GadgetDrawCommand::Kind::Text;
		c.window = "ToolTipText";
		c.text = m_lines[i];
		c.font = m_font; // the draw list's fonts are at the stage's size (the device scales them by min(scale))
		c.color = m_color;
		c.x0 = left;
		c.y0 = top + (int)(lineH * (float)i);
		c.x1 = left + (int)tw;
		c.y1 = c.y0 + (int)lineH;
		c.wrapCentered = true;
		out.commands.push_back(c);
	}
}

std::vector<std::string> CursorTooltip::acceptanceStops()
{
	return { "[S-4610] menu tool tips (HOVER-1): ported RW 0x624C68's path (the hovered button's own path), RW 0x6235E2 (the AptButtonTooltipMap name, a label only "
	         "when the string file has it), Mouse::setCursorTooltip / drawTooltip (RW 0x5EE1A6 / 0x5EE7A8: TooltipDelayTime, the 4-update hide) and GuiFX's "
	         "ShowToolTip / MoveToolTip / HideToolTip / ToolTipText (RW 0x782DD9 / 0x782CDC / 0x782CA0 / 0x782D52), in the front end and in the quit menu; not "
	         "shown: My Heroes' and Quit's buttons (the main menu leaves their clips on frame 0), ScriptEngine + 0x1A238's gate is not read; not fetched, named by "
	         "the inventory of every tool tip source (tools/hover/hover_inventory.py, sources.csv): 18 lobby, staging and map list tips (TOOLTIP:AdvSetting, "
	         "BuddyPlayer, ChatPlayerInfo, GameStarted, GenericPlayer, LadderRankUnavailable, LocalPlayer, MapEasySuccess, MapHardMaxSuccess, MapHardSuccess, "
	         "MapMediumSuccess, MapNoSuccess, MpSaveGame, PasswordIcon, ProfiledPlayer, StagingPlayerInfo, UserMapIcon, Skirmish/tooltipPlayerLevelIcon), 4 "
	         "Create-a-Hero builder tips (TOOLTIP:CAH_CURRENT_POWER, CAH_IN_PALANTIR, CAH_NO_CURRENT_POWER, CAH_PALANTIR_EMPTY_SLOT), the quit menu's "
	         "TOOLTIP:QuitMenu/Surrender/WOTRSurrender, the TOOLTIPTEXT of the native gadgets (the experience screen's two, the load screen's progress bar) and 10 "
	         "Mouse.ini presentation fields; INFERENCE: the string's pixel size is the font metrics' stage measure times min(scale), ToolTipText places it in stage "
	         "units" };
}

// ---------------------------------------------------------------------------------------------------------------------------------
// ShellCursorTooltip
// ---------------------------------------------------------------------------------------------------------------------------------
ShellCursorTooltip::ShellCursorTooltip(WindowManager &windows, const GameTextSource *text, const CursorTooltipSettings &settings)
	: m_windows(windows), m_text(text), m_settings(settings)
{
	m_windows.setRenderCallback("ToolTipText", [this](float x, float y, float w, float h, GadgetDrawList &out) {
		m_tip.render(x, y, w, h, m_scaleX, m_scaleY, out);
	});
	for (const std::string &l : CursorTooltip::acceptanceStops())
	{
		m_windows.note("stop", l); // the shell's report lists its notes ([S-4610])
	}
}

ShellCursorTooltip::~ShellCursorTooltip()
{
	m_windows.clearRenderCallback("ToolTipText");
}

void ShellCursorTooltip::update(std::uint32_t nowMs, float windowW, float windowH, FontMetricsSource &metrics)
{
	// the pointer: the Apt player's own (the position its last move event set, gApt + 0x74 / + 0x78), the one the hover was found at
	float stageX = 0.0f, stageY = 0.0f;
	m_windows.apt().mousePosition(stageX, stageY);
	// RW 0x6235E2: the label's text, only when the string file has it
	const std::string label = m_windows.currentTooltipLabel();
	UnicodeString text;
	bool exists = false;
	if (!label.empty())
	{
		const UnicodeString fetched = fetchOrMissing(m_text, label, &exists);
		if (exists)
		{
			text = fetched;
		}
	}
	m_label = exists ? label : std::string();
	m_tip.setTooltip(text, -1);
	// the window's scale (the Apt window manager's dimensions, RW 0x782DF8 vslot 0x3C): window pixels per stage unit
	AptStageMapping mapping;
	mapping.windowW = windowW;
	mapping.windowH = windowH;
	m_scaleX = mapping.scaleX();
	m_scaleY = mapping.scaleY();
	std::vector<CursorTooltip::Call> calls;
	m_tip.update(nowMs, stageX, stageY, m_settings, m_scaleX, m_scaleY, metrics, calls);
	if (calls.empty())
	{
		return;
	}
	const int level = m_windows.findAptMovieIndex("GuiFX.apt");
	for (const CursorTooltip::Call &c : calls)
	{
		std::string line = c.function;
		for (const std::string &a : c.args)
		{
			line += " " + a;
		}
		m_calls.push_back(line);
		std::string error;
		if (level < 0 || !m_windows.invokeASAt(level, "", c.function, c.args, nullptr, &error))
		{
			if (m_errors.size() < 50)
			{
				m_errors.push_back(c.function + ": " + (level < 0 ? std::string("GuiFX.apt is not loaded") : error));
			}
		}
	}
}
