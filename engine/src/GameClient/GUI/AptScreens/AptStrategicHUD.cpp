// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptStrategicHUD.h.  Lane WOTR-2.

#include "GameClient/GUI/AptScreens/AptStrategicHUD.h"

#include "GameClient/GUI/AptMessageBox.h"

#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/ControlBarRadialMenu.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"
#include "GameClient/MapCache.h"
#include "GameClient/GUI/Skirmish/LivingWorldShellInfo.h"
#include "GameClient/GUI/Skirmish/LivingWorldSetupText.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace
{
// UTF-8 -> UTF-16 (the help's draw ops are UTF-8; the shell's text commands UnicodeString)
UnicodeString utf8ToU16(const std::string &in)
{
	UnicodeString out;
	for (size_t i = 0; i < in.size();)
	{
		const unsigned char c = (unsigned char)in[i];
		std::uint32_t v = c;
		int extra = 0;
		if (c >= 0xF0)
		{
			v = c & 0x07;
			extra = 3;
		}
		else if (c >= 0xE0)
		{
			v = c & 0x0F;
			extra = 2;
		}
		else if (c >= 0xC0)
		{
			v = c & 0x1F;
			extra = 1;
		}
		++i;
		for (int k = 0; k < extra && i < in.size(); ++k, ++i)
		{
			v = (v << 6) | ((unsigned char)in[i] & 0x3F);
		}
		if (v >= 0x10000)
		{
			v -= 0x10000;
			out.push_back((char16_t)(0xD800 + (v >> 10)));
			out.push_back((char16_t)(0xDC00 + (v & 0x3FF)));
		}
		else
		{
			out.push_back((char16_t)v);
		}
	}
	return out;
}
} // namespace

namespace
{
// TypeImages[AutoResolveUnitType] (WB 0x1655730); the key table RW 0xC91470 (0 .. 8)
std::string unitTypeImageOf(const StrategicHudData *hud, int type)
{
	static const char *const kUnitTypes[9] = { "UnitSoldier", "UnitArcher", "UnitPikemen", "UnitCavalry", "UnitMonster", "UnitHero", "UnitFortress",
		"UnitSiege", "UnitSupport" };
	if (!hud || type < 0 || type > 8)
	{
		return std::string();
	}
	auto it = hud->typeImages.find(kUnitTypes[type]);
	return it == hud->typeImages.end() ? std::string() : it->second;
}

// a CSF format with one argument (UnicodeString::format with %d / %s / a float conversion): every conversion takes the one value
std::string formatOne(const std::string &format, const std::string &intText, double real, bool isReal)
{
	std::string out;
	for (size_t i = 0; i < format.size(); ++i)
	{
		if (format[i] != '%' || i + 1 >= format.size())
		{
			out += format[i];
			continue;
		}
		if (format[i + 1] == '%')
		{
			out += '%';
			++i;
			continue;
		}
		size_t j = i + 1;
		while (j < format.size() && (format[j] == '.' || format[j] == '-' || format[j] == '+' || (format[j] >= '0' && format[j] <= '9')))
		{
			++j;
		}
		if (j >= format.size())
		{
			out += format.substr(i);
			break;
		}
		const char conv = format[j];
		if (conv == 'f' || conv == 'g')
		{
			char buf[64];
			std::snprintf(buf, sizeof buf, ("%" + format.substr(i + 1, j - i - 1) + conv).c_str(), isReal ? real : (double)std::atoi(intText.c_str()));
			out += buf;
		}
		else
		{
			out += intText;
		}
		i = j;
	}
	return out;
}
} // namespace

const std::vector<std::string> &AptStrategicHUD::partNames()
{
	static const std::vector<std::string> names = { "Checklist", "EndTurnButton", "HelpBox", "LoadDialogFrame", "NewTurnIndicator", "Palantir", "RadialMenuStage",
		"SelectionDetails", "StatsDisplay" }; // RW 0x83A272's registration order
	return names;
}

AptStrategicHUD::AptStrategicHUD(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "StrategicHUD.apt", "StrategicHUD"), m_env(environment)
{
	registerCommand("StrategicHUD::OnInitialized", [this](const std::string &arg) { m_log.push_back("StrategicHUD::OnInitialized(" + arg + ")"); });
	registerCommand("StrategicHUD::OnClosed", [this](const std::string &arg) { m_log.push_back("StrategicHUD::OnClosed(" + arg + ")"); });
	if (level() < 0)
	{
		return;
	}
	const std::string prefix = "_level" + std::to_string(level()); // RW 0x83A354: "_level%u"
	for (const std::string &part : partNames())
	{
		registerCommand(prefix + "_On" + part + "Loaded", [this, part](const std::string &arg) {
			m_log.push_back("_On" + part + "Loaded(" + arg + ")");
			onPartLoaded(part, arg);
		});
		registerCommand(prefix + "_On" + part + "Unloaded", [this, part](const std::string &arg) {
			m_log.push_back("_On" + part + "Unloaded(" + arg + ")");
			onPartUnloaded(part);
		});
	}
}

AptStrategicHUD::~AptStrategicHUD()
{
	clearNotice();
	for (const ChecklistRow &r : m_rows)
	{
		const std::string base = "_level" + std::to_string(level()) + "." + r.path;
		windows().unregisterProvider(base + "_TextWidth");
		windows().unregisterProvider(base + "_Height");
		windows().clearRenderCallback(base + "_RenderText");
	}
	if (!m_helpBox.renderName().empty())
	{
		windows().clearRenderCallback(m_helpBox.renderName()); // review wotr2m4r2: the help box's render callback holds this
	}
}

std::string AptStrategicHUD::relativePath(const std::string &clip) const
{
	// [S-1334 inference] the path without the window's "_levelN." prefix
	if (clip.compare(0, 6, "_level") == 0)
	{
		const size_t dot = clip.find('.');
		if (dot != std::string::npos)
		{
			return clip.substr(dot + 1);
		}
	}
	return clip;
}

namespace
{
// "index=2&name=_level2.main.globe.cmd.Button2": the value of `key` (the frame commands' argument, RW 0x9E1AFF / RW 0x81560E)
std::string queryValueOf(const std::string &arg, const std::string &key)
{
	size_t at = 0;
	while (at <= arg.size())
	{
		const size_t amp = arg.find('&', at);
		const std::string pair = arg.substr(at, amp == std::string::npos ? std::string::npos : amp - at);
		const size_t eq = pair.find('=');
		if (eq != std::string::npos && pair.compare(0, eq, key) == 0)
		{
			return pair.substr(eq + 1);
		}
		if (amp == std::string::npos)
		{
			break;
		}
		at = amp + 1;
	}
	return std::string();
}
} // namespace

std::string AptStrategicHUD::text(const std::string &label) const
{
	return loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, label));
}

void AptStrategicHUD::onPartLoaded(const std::string &part, const std::string &clip)
{
	if (m_parts.count(part))
	{
		return; // RW 0x839CE0 ..: the part's object is made once
	}
	m_parts[part] = clip;
	const std::string base = "_level" + std::to_string(level()) + "." + relativePath(clip); // RW 0x983517: "_level%u." + path
	std::vector<std::string> &names = m_partCommands[part];
	auto add = [&](const std::string &suffix, WindowManager::CommandFn fn) {
		const std::string name = base + suffix;
		if (registerCommand(name, std::move(fn)))
		{
			names.push_back(name);
		}
	};
	if (part == "EndTurnButton")
	{
		add("_OnClicked", [this](const std::string &) { // RW 0x98347F
			m_log.push_back("EndTurnButton_OnClicked");
			m_requests.push_back("end_turn");
		});
	}
	else if (part == "StatsDisplay")
	{
		add("_OnStatRollOver", [this](const std::string &arg) { m_log.push_back("Stats_OnStatRollOver(" + arg + ")"); }); // RW 0x983BA3 (tooltip: not ported)
		add("_OnStatRollOut", [this](const std::string &arg) { m_log.push_back("Stats_OnStatRollOut(" + arg + ")"); });   // RW 0x983C22
		writeStats();
	}
	else if (part == "LoadDialogFrame")
	{
		add("_OnContentLoaded", [this](const std::string &arg) { // RW 0x9862FC
			m_log.push_back("DialogFrame_OnContentLoaded(" + arg + ")");
			onContentLoaded(arg);
		});
		if (m_promptWanted)
		{
			openBattlePrompt(m_promptRegion, m_promptButtons[0], m_promptButtons[1], m_promptButtons[2], m_promptAlly, m_promptEnemy, m_promptRealTimeReason);
		}
	}
	else if (part == "Checklist")
	{
		const std::string path = relativePath(clip);
		add("_OnExpandButtonClicked", [this, path](const std::string &) { // RW 0x984CA4
			m_log.push_back("Checklist_OnExpandButtonClicked");
			const char *fn = m_checklistState == 2 ? "CloseList" : (m_checklistState == 0 ? "OpenList" : nullptr);
			if (!fn)
			{
				return; // opening or closing: the click is ignored
			}
			std::string error;
			if (!windows().invokeASAt(level(), path, fn, {}, nullptr, &error))
			{
				windows().note("invoke-failed", std::string("StrategicChecklist ") + fn + ": " + error);
				return;
			}
			m_checklistState = m_checklistState == 2 ? 3 : 1; // RW 0x984991 / 0x9849C5
		});
		add("_OnOpen", [this](const std::string &) { // RW 0x98447C
			m_log.push_back("Checklist_OnOpen");
			if (m_checklistState == 1)
			{
				m_checklistState = 2;
			}
		});
		add("_OnClosed", [this, path](const std::string &) { // RW 0x98446F
			m_log.push_back("Checklist_OnClosed");
			if (m_checklistState == 3)
			{
				m_checklistState = 0;
			}
			if (m_openWhenClosed && m_checklistState == 0) // the update's "open once closed" (RW 0x985531, + 0x26)
			{
				m_openWhenClosed = false;
				if (checklistCall(path, "OpenList", {}))
				{
					m_checklistState = 1;
				}
			}
		});
		add("_OnScrollBarLoaded", [this](const std::string &arg) { m_log.push_back("Checklist_OnScrollBarLoaded(" + arg + ")"); });   // RW 0x985591 (the item list's scroll bar: not ported)
		add("_OnScrollBarUnloaded", [this](const std::string &arg) { m_log.push_back("Checklist_OnScrollBarUnloaded(" + arg + ")"); }); // RW 0x984574
		// RW 0x985642: the empty list's message
		windows().setAptText("APT:StrategicHUDNoChecklistItems", text("STRATEGICHUD:CriticalTasksWillBeDisplayedHere"));
		writeTurn(false);
		m_checklistPath = path;
		windows().note("unported-component", "StrategicHUD part Checklist (" + clip + "): the item list's scroll bar (VerticalScrollBar.swf) is not ported: rows past the list's height stay hidden [S-1334]");
	}
	else if (part == "Palantir")
	{
		// RW 0x982CC0: the palantir's own commands. The region UI, selection UI and region stats tray objects (RW 0x9E2CC9 / 0x9E3411 / 0x9E2C20) and
		// the options / objectives buttons' bodies are not ported: their loads are logged
		add("_OnRegionStatsTrayLoaded", [this](const std::string &arg) { // RW 0x9827A1 -> the tray RW 0x9E2C20 (once until it unloads)
			m_log.push_back("Palantir_OnRegionStatsTrayLoaded(" + arg + ")");
			if (!m_regionStatsClip.empty())
			{
				return;
			}
			m_regionStatsClip = relativePath(arg);
			const std::string base = "_level" + std::to_string(level()) + "." + m_regionStatsClip;
			std::vector<std::string> &names = m_partCommands["Palantir"];
			for (const char *n : { "BuildPlots", "ArmoryPoints", "CommandPoints" })
			{
				// the hot spots' help (RW 0x9DAB43): RegionDisplay's <n>Title / <n>Help, shown at once; the rollout is the guarded hide (RW 0x9F3AC8)
				const std::string name = n;
				auto rollOver = [this, name](const std::string &) {
					const StrategicHudData *hud = m_env.livingWorld ? &m_env.livingWorld->strategicHud : nullptr;
					std::string title, help;
					if (hud)
					{
						auto t = hud->regionDisplay.find(name + "Title");
						auto h = hud->regionDisplay.find(name + "Help");
						title = t != hud->regionDisplay.end() ? t->second : std::string();
						help = h != hud->regionDisplay.end() ? h->second : std::string();
					}
					showHelp(title, help);
				};
				if (registerCommand(base + "_On" + n + "RollOver", rollOver))
				{
					names.push_back(base + "_On" + n + "RollOver");
				}
				if (registerCommand(base + "_On" + n + "RollOut", [this](const std::string &) { hideHelp(); }))
				{
					names.push_back(base + "_On" + n + "RollOut");
				}
			}
			m_regionStatsShown = RegionStats();
			writeRegionStats(true);
		});
		add("_OnRegionStatsTrayUnloaded", [this](const std::string &arg) { // RW 0x98265F
			m_log.push_back("Palantir_OnRegionStatsTrayUnloaded(" + arg + ")");
			if (m_regionStatsClip.empty())
			{
				return;
			}
			const std::string base = "_level" + std::to_string(level()) + "." + m_regionStatsClip;
			std::vector<std::string> &names = m_partCommands["Palantir"];
			for (const char *n : { "BuildPlots", "ArmoryPoints", "CommandPoints" })
			{
				for (const char *k : { "RollOver", "RollOut" })
				{
					const std::string name = base + "_On" + n + k;
					windows().unregisterCommand(name);
					names.erase(std::remove(names.begin(), names.end(), name), names.end());
				}
			}
			m_regionStatsClip.clear();
		});
		for (const char *c : { "_OnRegionUILoaded", "_OnRegionUIUnloaded", "_OnSelectionUILoaded",
				 "_OnSelectionUIUnloaded", "_OnOptionsButtonClicked", "_OnOptionsButtonRollOver", "_OnOptionsButtonRollOut", "_OnObjectivesButtonClicked",
				 "_OnObjectivesButtonRollOver", "_OnObjectivesButtonRollOut" })
		{
			const std::string cmd = c;
			add(cmd, [this, cmd](const std::string &arg) { m_log.push_back("Palantir" + cmd + "(" + arg + ")"); });
		}
		add("_OnCommandUILoaded", [this](const std::string &arg) { // RW 0x9826FF -> the command UI RW 0x9E21B5 (once)
			m_log.push_back("Palantir_OnCommandUILoaded(" + arg + ")");
			if (!m_commandUiClip.empty())
			{
				return;
			}
			m_commandUiClip = arg;
			const std::string cbase = "_level" + std::to_string(level()) + "." + relativePath(arg);
			std::vector<std::string> &names = m_partCommands["Palantir"];
			auto cadd = [&](const std::string &suffix, WindowManager::CommandFn fn) {
				if (registerCommand(cbase + suffix, std::move(fn)))
				{
					names.push_back(cbase + suffix);
				}
			};
			cadd("_OnButtonFrameLoaded", [this](const std::string &a) { // RW 0x9E1C93
				const std::string index = queryValueOf(a, "index");
				const int i = std::atoi(index.c_str());
				if (index.empty() || i < 0 || i > 5)
				{
					return;
				}
				if (m_commandFrames[i].empty())
				{
					m_commandFrames[i] = relativePath(queryValueOf(a, "name"));
				}
				updateCommandUI();
			});
			cadd("_OnButtonFrameUnloaded", [this](const std::string &a) { // RW 0x9E1D9F
				const int i = std::atoi(queryValueOf(a, "index").c_str());
				if (i >= 0 && i <= 5)
				{
					m_commandFrames[i].clear();
					if (i == 0)
					{
						m_toggle = ToggleSlot();
					}
					else
					{
						const CommandButton want = m_slots[i].want;
						m_slots[i] = CommandSlot();
						m_slots[i].want = want;
					}
				}
			});
			for (const char *c : { "_OnSubMenuLoaded", "_OnSubMenuUnloaded", "_OnToggleFlashLoaded", "_OnToggleFlashUnloaded" }) // RW 0x9E1DC6 .. 0x9E2008: not ported
			{
				const std::string cmd = c;
				cadd(cmd, [this, cmd](const std::string &a) { m_log.push_back("CommandUI" + cmd + "(" + a + ")"); });
			}
		});
		add("_OnCommandUIUnloaded", [this](const std::string &arg) { // RW 0x982796
			m_log.push_back("Palantir_OnCommandUIUnloaded(" + arg + ")");
			m_commandUiClip.clear();
			for (std::string &f : m_commandFrames)
			{
				f.clear();
			}
			m_toggle = ToggleSlot();
			for (int i = 1; i <= 5; ++i)
			{
				const CommandButton want = m_slots[i].want;
				m_slots[i] = CommandSlot();
				m_slots[i].want = want;
			}
		});
		windows().note("unported-component", "StrategicHUD part Palantir (" + clip + "): the command UI's slots (0 the toggle selection details "
			"button, 1..5 the selections' buttons) and the region stats tray are ported; the region UI, the selection UI and the sub menus are not [S-1334]");
	}
	else if (part == "SelectionDetails")
	{
		m_detailsState = 0; // RW 0x985DD6: closed, disabled
		m_detailsEnabled = false;
		m_detailsSeen = false;
		add("_OnToggleButtonClicked", [this](const std::string &) { // RW 0x985CC2
			m_log.push_back("SelectionDetails_OnToggleButtonClicked");
			if (m_detailsState == 0)
			{
				openDetailsTray(true);
			}
			else if (m_detailsState == 2)
			{
				openDetailsTray(false);
			}
		});
		add("_OnOpened", [this](const std::string &) { // RW 0x985D16
			m_log.push_back("SelectionDetails_OnOpened");
			if (m_detailsState == 1)
			{
				m_detailsState = 2;
			}
			updateCommandUI();
		});
		add("_OnClosed", [this](const std::string &) { // RW 0x985CFB
			m_log.push_back("SelectionDetails_OnClosed");
			if (m_detailsState == 3)
			{
				m_detailsState = 0;
			}
			updateCommandUI();
		});
		add("_OnPanelFrameLoaded", [this](const std::string &arg) { // RW 0x985A89: the panel frame (a dialog frame, RW 0x9E461E -> RW 0x98648F)
			m_log.push_back("SelectionDetails_OnPanelFrameLoaded(" + arg + ")");
			if (!m_panelFrame.empty())
			{
				return;
			}
			m_panelFrame = relativePath(arg);
			const std::string fbase = "_level" + std::to_string(level()) + "." + m_panelFrame;
			if (registerCommand(fbase + "_OnContentLoaded", [this](const std::string &clip) { onTrayContentLoaded(clip); }))
			{
				m_partCommands["SelectionDetails"].push_back(fbase + "_OnContentLoaded");
			}
			syncTrayContent();
		});
		add("_OnPanelFrameUnloaded", [this](const std::string &arg) { // RW 0x985B1D
			m_log.push_back("SelectionDetails_OnPanelFrameUnloaded(" + arg + ")");
			unloadTrayContent();
			m_panelFrame.clear();
			m_trayLoading = TRAY_NONE;
		});
		windows().note("unported-component", "StrategicHUD part SelectionDetails (" + clip + "): the tray, the region panel (territory, armies, structures) and "
			"the build queue panel are ported; the army and hero army details and the retinue panels are not [S-1334]");
	}
	else if (part == "RadialMenuStage")
	{
		m_radialStage = relativePath(clip);
		const std::string sbase = "_level" + std::to_string(level()) + "." + m_radialStage;
		for (int id = 0; id < 16; ++id)
		{
			add("_OnButtonFrameLoaded_" + std::to_string(id), [this, id](const std::string &frame) { onRadialFrameLoaded(id, frame); });
			add("_OnButtonFrameUnloaded_" + std::to_string(id), [this, id](const std::string &) {
				if (id < (int)m_radialButtons.size())
				{
					m_radialButtons[(size_t)id].frame.clear();
					m_radialButtons[(size_t)id].initialized = false;
				}
			});
		}
		(void)sbase;
	}
	else if (part == "HelpBox")
	{
		// _OnHelpBoxLoaded RW 0x839D7F: the help box object (InGameHelpBoxMovieClip RW 0x92E689: the custom render "<clip>_Content")
		const std::string path = "_level" + std::to_string(level()) + "." + relativePath(clip);
		m_helpBox.loaded(path, [this](const std::string &p, const std::string &fn, const std::vector<std::string> &args) {
			std::string err;
			const std::string prefix = "_level" + std::to_string(level()) + ".";
			const std::string rel = p.rfind(prefix, 0) == 0 ? p.substr(prefix.size()) : p;
			return windows().invokeASAt(level(), rel, fn, args, nullptr, &err);
		});
		windows().setRenderCallback(m_helpBox.renderName(), [this](float x, float y, float w, float h, GadgetDrawList &out) {
			// RW 0x92E2AC: the content clip's rectangle (stage units here: the shell scales the texts) samples the width, then draws the help
			std::vector<HelpDrawOp> ops;
			m_helpBox.render(x, y, w, h, ops);
			for (const HelpDrawOp &op : ops)
			{
				if (op.kind != HelpDrawOp::TEXT || op.text.empty())
				{
					continue;
				}
				GadgetDrawCommand c;
				c.kind = GadgetDrawCommand::Kind::Text;
				c.text = utf8ToU16(op.text);
				c.font.name = op.font.name;
				c.font.pointSize = op.font.pointSize;
				c.font.bold = op.font.bold;
				c.color = op.font.color;
				// the help's lines are centred in their box (CommandButtonHelp::emit gives the box's left edge and width)
				const int tw = m_helpMetrics ? m_helpMetrics->textWidth(c.font, c.text) : 0;
				c.x0 = (int)(op.x + (op.w - (float)tw) * 0.5f + 0.5f);
				c.y0 = (int)op.y;
				out.commands.push_back(c);
			}
		});
	}
	else if (part == "NewTurnIndicator")
	{
		add("_OnDone", [this](const std::string &) { // RW 0x986098
			m_log.push_back("NewTurnIndicator_OnDone");
			m_requests.push_back("new_turn_done");
		});
	}
	else
	{
		windows().note("unported-component", "StrategicHUD part " + part + " (" + clip + "): its object's body is not ported [S-1334]");
	}
}

void AptStrategicHUD::onPartUnloaded(const std::string &part)
{
	if (part == "HelpBox")
	{
		windows().clearRenderCallback(m_helpBox.renderName()); // _OnHelpBoxUnloaded RW 0x839E37
		m_helpBox.unloaded();
		m_helpShown.reset();
	}
	for (const std::string &name : m_partCommands[part])
	{
		windows().unregisterCommand(name);
	}
	m_partCommands.erase(part);
	m_parts.erase(part);
	if (part == "Palantir")
	{
		m_commandUiClip.clear();
		for (std::string &f : m_commandFrames)
		{
			f.clear();
		}
		m_toggle = ToggleSlot();
	}
	if (part == "SelectionDetails")
	{
		unloadTrayContent();
		m_panelFrame.clear();
		m_trayLoading = TRAY_NONE;
	}
	if (part == "Checklist")
	{
		m_checklistState = 0; // the part's object goes with it (a reload makes a new one, RW 0x985642: closed, the arrow not yet sent)
		m_checklistButtonUp = false;
	}
}

void AptStrategicHUD::setStats(const StrategicHudStats &stats)
{
	m_stats = stats;
	m_haveStats = true;
	writeStats();
}

void AptStrategicHUD::writeStats()
{
	auto it = m_parts.find("StatsDisplay");
	if (!m_haveStats || it == m_parts.end())
	{
		return;
	}
	const std::string path = relativePath(it->second);
	auto record = [&](int line, const std::string &value) { // RW 0x983B39: "APT:_level%u.%s%d_Text"
		// the stat clips are named by their line under the stats clip ("$_level1.main.stats.0_Text" in the retail movie): the part's path ends in '.'
		const std::string name = "APT:_level" + std::to_string(level()) + "." + path + "." + std::to_string(line) + "_Text";
		m_statRecords[name] = value;
		windows().setAptText(name, value);
	};
	record(0, formatOne(text("STRATEGICHUD:StatsTreasury"), std::to_string(m_stats.treasury), 0.0, false));
	record(6, formatOne(text("STRATEGICHUD:StatsTreasuryIncome"), std::to_string(m_stats.income), 0.0, false));
	record(4, formatOne(text("STRATEGICHUD:StatsResourceMultiplier"), std::string(), (double)m_stats.resourceMultiplier, true));
	record(5, formatOne(text("STRATEGICHUD:StatsPowerPoints"), std::to_string(m_stats.powerPoints), 0.0, false));
	for (int i = 0; i < 3; ++i)
	{
		record(i + 1, formatOne(text("STRATEGICHUD:StatsBonus"), std::to_string(m_stats.bonus[i]), 0.0, false));
	}
}

// RW 0x97EE63: the toggle selection details command in the palantir's command slot 0 once the slot's frame exists (RW 0x9E1C93), the slot is free and
// the help box part is loaded; its button (RW 0x9DB9FC) shows ToggleSelectionDetailsButton's Image. The content is the movie's CommandButton made in the
// frame (CreateContent, as the in-game palantir's, AptPalantir::createContent); a press opens / closes the selection details tray (RW 0x9DB97E)
void AptStrategicHUD::updateCommandUI()
{
	updateDetailsTray();
	updateToggleSlot();
	updateCommandSlots();
}

void AptStrategicHUD::showNotice(const std::string &titleLabel, const std::string &textLabel, const std::vector<NoticeArg> &args,
	const std::vector<NoticeItem> &items, bool question)
{
	auto fetch = [this](const std::string &label) { return fetchOrMissing(m_env.gameText, label); };
	auto formatArgs = [&](const std::vector<NoticeArg> &in) {
		std::vector<LivingWorldSetupText::FormatArg> out;
		for (const NoticeArg &a : in)
		{
			out.push_back(LivingWorldSetupText::FormatArg(a.label ? (a.value.empty() ? std::u16string() : fetch(a.value)) : utf8ToU16(a.value)));
		}
		return out;
	};
	const std::vector<LivingWorldSetupText::FormatArg> fa = formatArgs(args);
	const std::u16string title = LivingWorldSetupText::format(fetch(titleLabel), fa);
	std::u16string text = LivingWorldSetupText::format(fetch(textLabel), fa);
	for (const NoticeItem &it : items)
	{
		text += LivingWorldSetupText::format(fetch(it.label), formatArgs(it.args));
	}
	if (!m_noticeBox)
	{
		m_noticeBox = std::make_unique<AptMessageBox>(windows(), shell(), "StrategicMessageBox");
	}
	if (m_noticeBox->level() < 0)
	{
		windows().note("strategic-notice", "no StrategicMessageBox (GuiFX.apt is not loaded): the notice cannot be shown"); // rule 10: never silent
		return;
	}
	m_noticeShown = true;
	m_waitShown = false; // WOTR2 milestone 8: a notice replaces the wait message in the box (it shows again after the notice's OK)
	m_noticeAnswer = -1;
	AptMessageBox::ButtonFn onButton;
	if (question)
	{
		// RW 0x8166CD (the save request) / RW 0x8160B3 (the leave vote): button 2 Yes, 3 No
		onButton = [this](int button) { m_noticeAnswer = button == AptMessageBox::BUTTON_YES ? 1 : button == AptMessageBox::BUTTON_NO ? 0 : -1; };
	}
	m_noticeBox->show(question ? AptMessageBox::TYPE_YES_NO : AptMessageBox::TYPE_OK, title, text, onButton, [this](int state) {
		if (state == 3)
		{
			m_noticeShown = false; // RW 0x900B6D: + 0x25 = 0, + 0x26 = 1
		}
	});
}

void AptStrategicHUD::showNoticeText(const std::u16string &title, const std::u16string &text)
{
	if (!m_noticeBox)
	{
		m_noticeBox = std::make_unique<AptMessageBox>(windows(), shell(), "StrategicMessageBox");
	}
	if (m_noticeBox->level() < 0)
	{
		windows().note("strategic-notice", "no StrategicMessageBox (GuiFX.apt is not loaded): the notice cannot be shown"); // rule 10: never silent
		return;
	}
	m_noticeShown = true;
	m_waitShown = false;
	m_noticeBox->show(AptMessageBox::TYPE_OK, title, text, AptMessageBox::ButtonFn(), [this](int state) {
		if (state == 3)
		{
			m_noticeShown = false;
		}
	});
}

void AptStrategicHUD::showMapHelp(const MapHelp &h)
{
	if (!m_helpBox.isLoaded() || h.kind == MapHelp::NONE)
	{
		return; // RW 0x97E6DD: no help box, no help shown
	}
	auto fetch = [this](const std::string &label) { return label.empty() ? std::u16string() : fetchOrMissing(m_env.gameText, label); };
	std::u16string title, text;
	if (h.kind == MapHelp::REGION)
	{
		const LivingWorldSetupCampaign *c = m_env.livingWorld ? m_env.livingWorld->findCampaign(h.regionCampaign) : nullptr;
		if (!c || h.region < 0 || h.region >= (int)c->regions.size())
		{
			windows().note("strategic-help", "no region " + std::to_string(h.region) + " in the region campaign '" + h.regionCampaign + "'");
			return;
		}
		title = fetch(c->regions[(size_t)h.region].displayLabel);
		text = LivingWorldSetupText::regionDescription(*c, h.region, m_env.gameText);
	}
	else if (h.kind == MapHelp::ARMY)
	{
		title = fetch(h.titleLabel);
		for (const std::string &l : h.heroLabels)
		{
			text += (text.empty() ? u"" : u", ") + fetch(l);
		}
		for (const auto &line : h.lines)
		{
			std::u16string s = LivingWorldSetupText::format(fetch("BANNERUI:SummaryUnitQuantity"), { LivingWorldSetupText::FormatArg(fetch(line.first)) });
			s = LivingWorldSetupText::format(s, { LivingWorldSetupText::FormatArg(line.second) });
			text += (text.empty() ? u"" : u"\n") + s;
		}
	}
	else
	{
		title = fetch(h.titleLabel);
		text = fetch(h.textLabel);
		if (h.farmTreasury >= 0)
		{
			text += LivingWorldSetupText::format(fetch("CONTROLBAR:LW_FarmTreasuryBonus"), { LivingWorldSetupText::FormatArg(h.farmTreasury) });
		}
	}
	m_mapHelp = std::make_shared<CommandButtonHelp>(CommandButtonHelp::simple(title, text));
	m_helpBox.setHelp(m_mapHelp);
	helpUpdate();
}

void AptStrategicHUD::hideMapHelp()
{
	if (m_mapHelp && m_helpBox.help() == m_mapHelp.get())
	{
		m_helpBox.hideSoon();
	}
	m_mapHelp.reset();
}

bool AptStrategicHUD::checklistCall(const std::string &path, const std::string &fn, const std::vector<AptValue> &args)
{
	std::string error;
	if (!windows().invokeASAtValues(level(), path, fn, args, &error))
	{
		windows().note("invoke-failed", "StrategicChecklist " + fn + ": " + error);
		return false;
	}
	return true;
}

void AptStrategicHUD::setChecklistItems(const std::vector<ChecklistItem> &items)
{
	auto format = [this](const ChecklistItem &it) {
		std::vector<LivingWorldSetupText::FormatArg> fa;
		for (const NoticeArg &a : it.args)
		{
			fa.push_back(LivingWorldSetupText::FormatArg(a.label ? (a.value.empty() ? std::u16string() : fetchOrMissing(m_env.gameText, a.value)) : utf8ToU16(a.value)));
		}
		return LivingWorldSetupText::format(fetchOrMissing(m_env.gameText, it.label), fa);
	};
	// gone keys first (RW 0x98541B), back to front
	for (size_t i = m_rows.size(); i-- > 0;)
	{
		const bool kept = std::any_of(items.begin(), items.end(), [&](const ChecklistItem &x) { return x.key == m_rows[i].item.key; });
		if (!kept)
		{
			deleteChecklistRow(i);
		}
	}
	for (const ChecklistItem &it : items)
	{
		auto row = std::find_if(m_rows.begin(), m_rows.end(), [&](const ChecklistRow &r) { return r.item.key == it.key; });
		if (row != m_rows.end())
		{
			// the setters forward a value only when it changed (RW 0x9DC351 / 0x9DC0EF / 0x9DC111)
			const std::u16string text = format(it);
			if (text != row->text)
			{
				row->text = text;
				row->heightKnown = false; // setText RW 0x9E3CF2: the height again
			}
			if (it.importance != row->item.importance)
			{
				row->visibilitySent = false;
				m_relayout = true;
			}
			if (it.checked != row->item.checked)
			{
				row->checkedSent = false;
			}
			row->item = it;
			continue;
		}
		if (m_checklistPath.empty())
		{
			windows().note("strategic-checklist", "no checklist part: the row '" + it.key + "' cannot be made");
			continue;
		}
		// CreateNewItem RW 0x984FC6: the movie's CreateItem(counter) attaches ChecklistItem and writes its path to <checklist>_NewItem
		const std::string newItem = "_level" + std::to_string(level()) + "." + m_checklistPath + "_NewItem";
		std::string captured;
		windows().registerProvider(newItem, [&captured](const std::string &, std::string &value, bool setting) {
			if (setting)
			{
				captured = value;
			}
			return true;
		});
		checklistCall(m_checklistPath, "CreateItem", { AptValue::number((double)m_rowCounter) });
		windows().unregisterProvider(newItem);
		if (captured.empty())
		{
			windows().note("strategic-checklist", "CreateItem(" + std::to_string(m_rowCounter) + ") made no row"); // retail returns null: no row
			continue;
		}
		ChecklistRow r;
		r.item = it;
		r.text = format(it);
		r.name = m_rowCounter++;
		r.path = relativePath(captured);
		const std::string base = "_level" + std::to_string(level()) + "." + r.path;
		const std::string key = it.key;
		auto reg = [&](const std::string &suffix, WindowManager::CommandFn fn) {
			if (registerCommand(base + suffix, std::move(fn)))
			{
				r.names.push_back(base + suffix);
			}
		};
		reg("_OnClicked", [this, key](const std::string &) { // RW 0x9E360F -> the row's listener: select, then the creator's click
			m_log.push_back("Checklist_OnItemClicked(" + key + ")");
			m_requests.push_back("checklist:" + key);
		});
		reg("_OnRollOver", [this, key](const std::string &) { m_log.push_back("Checklist_OnItemRollOver(" + key + ")"); });
		reg("_OnRollOut", [this, key](const std::string &) { m_log.push_back("Checklist_OnItemRollOut(" + key + ")"); });
		m_rows.push_back(r);
		m_relayout = true;
	}
	if (m_rows.empty())
	{
		m_rowCounter = 0; // RW 0x98541B: the counter starts again with an empty list
	}
}

void AptStrategicHUD::deleteChecklistRow(size_t index)
{
	ChecklistRow &r = m_rows[index];
	const std::string base = "_level" + std::to_string(level()) + "." + r.path;
	for (const std::string &n : r.names)
	{
		windows().unregisterCommand(n);
	}
	windows().unregisterProvider(base + "_TextWidth");
	windows().unregisterProvider(base + "_Height");
	windows().clearRenderCallback(base + "_RenderText");
	checklistCall(m_checklistPath, "DeleteItem", { AptValue::number((double)r.name) });
	m_rows.erase(m_rows.begin() + (std::ptrdiff_t)index);
	m_relayout = true;
	if (m_rows.empty())
	{
		m_rowCounter = 0;
	}
}

bool AptStrategicHUD::hasIncompleteRequiredChecklistItem() const
{
	return std::any_of(m_rows.begin(), m_rows.end(), [](const ChecklistRow &r) { return r.item.importance == 1 && !r.item.checked; });
}

std::vector<std::string> AptStrategicHUD::flagIncompleteChecklistItems()
{
	std::vector<std::string> keys;
	if (!hasIncompleteRequiredChecklistItem())
	{
		return keys;
	}
	// RW 0x9DCEBB: ScrollToTop (no scroll bar here), Expand (closed: OpenList; closing: open once closed), Flash each incomplete required row
	if (m_checklistState == 0)
	{
		if (checklistCall(m_checklistPath, "OpenList", {}))
		{
			m_checklistState = 1;
		}
	}
	else if (m_checklistState == 3)
	{
		m_openWhenClosed = true;
	}
	for (const ChecklistRow &r : m_rows)
	{
		if (r.item.importance == 1 && !r.item.checked)
		{
			checklistCall(r.path, "Flash", {});
			keys.push_back(r.item.key);
		}
	}
	return keys;
}

void AptStrategicHUD::checklistUpdate()
{
	const StrategicHudData *hud = m_env.livingWorld ? &m_env.livingWorld->strategicHud : nullptr;
	const float spacing = hud ? hud->checklist.itemVerticalSpacing : 0.0f;
	GameFont font;
	if (hud)
	{
		font.name = hud->checklist.itemFont.name;
		font.pointSize = hud->checklist.itemFont.size;
		font.bold = hud->checklist.itemFont.bold;
		font.height = m_helpMetrics ? m_helpMetrics->fontHeight(font) : 0;
	}
	if (m_relayout)
	{
		// RW 0x9852AF: a stable sort by importance * 2 + new (every row stays new), then stacked with the spacing
		std::stable_sort(m_rows.begin(), m_rows.end(), [](const ChecklistRow &a, const ChecklistRow &b) { return a.item.importance > b.item.importance; });
		float y = 0.0f;
		for (ChecklistRow &r : m_rows)
		{
			y += spacing;
			if (r.y != y)
			{
				r.y = y;
				r.ySent = false;
			}
			y += r.height;
		}
		m_relayout = false;
	}
	for (ChecklistRow &r : m_rows)
	{
		const std::string base = "_level" + std::to_string(level()) + "." + r.path;
		if (!r.bound) // the row's externs and its text render, once
		{
			r.bound = true;
			const std::string key = r.item.key;
			for (const char *suffix : { "_TextWidth", "_Height" })
			{
				// review wotr2m6 (Sol): the row is found by its key at each call (m_rows may move; a row deleted meanwhile reads empty)
				const bool width = std::string(suffix) == "_TextWidth";
				windows().registerProvider(base + suffix, [this, key, width](const std::string &, std::string &value, bool setting) {
					auto it = std::find_if(m_rows.begin(), m_rows.end(), [&](const ChecklistRow &q) { return q.item.key == key; });
					if (it == m_rows.end())
					{
						value.clear();
						return true;
					}
					std::string &slot = width ? it->widthValue : it->heightValue;
					if (setting)
					{
						slot = value;
					}
					else
					{
						value = slot;
					}
					return true;
				});
			}
			windows().setRenderCallback(base + "_RenderText", [this, key](float x, float y, float, float h, GadgetDrawList &out) {
				// RW 0x9E362D: left-aligned, centred vertically, wrapped to the row's width
				auto it = std::find_if(m_rows.begin(), m_rows.end(), [&](const ChecklistRow &q) { return q.item.key == key; });
				const StrategicHudData *d = m_env.livingWorld ? &m_env.livingWorld->strategicHud : nullptr;
				if (it == m_rows.end() || !d || !it->widthKnown || !it->heightKnown)
				{
					return;
				}
				GadgetDrawCommand c;
				c.kind = GadgetDrawCommand::Kind::Text;
				c.text = it->text;
				c.font.name = d->checklist.itemFont.name;
				c.font.pointSize = d->checklist.itemFont.size;
				c.font.bold = d->checklist.itemFont.bold;
				const RGBAColorInt &col = it->item.importance == 1 ? d->checklist.itemImportantColor : d->checklist.itemNewColor;
				c.color = ((Color)col.alpha << 24) | ((Color)col.red << 16) | ((Color)col.green << 8) | (Color)col.blue;
				const float th = m_helpMetrics ? (float)m_helpMetrics->wrappedHeight(c.font, c.text, (int)it->width) : 0.0f;
				c.x0 = (int)std::floor(x + 0.5f);
				c.y0 = (int)std::floor((h - th + 1.0f) * 0.5f + y);
				c.wrapWidth = (int)it->width;
				out.commands.push_back(c);
			});
			r.names.push_back(base + "_RenderText");
		}
		// RW 0x9E3D78, stopping at the first step not done
		if (!r.widthKnown)
		{
			r.widthValue.clear();
			checklistCall(r.path, "GetTextWidth", {});
			if (r.widthValue.empty())
			{
				continue;
			}
			r.width = (float)std::floor(std::atof(r.widthValue.c_str()) + 0.5);
			r.widthKnown = true;
			r.heightKnown = false;
		}
		if (!r.heightKnown)
		{
			const int th = m_helpMetrics ? m_helpMetrics->wrappedHeight(font, r.text, (int)r.width) : 0;
			checklistCall(r.path, "SetHeight", { AptValue::number((double)th) });
			r.heightValue.clear();
			checklistCall(r.path, "GetHeight", {});
			if (r.heightValue.empty())
			{
				continue;
			}
			r.heightKnown = true;
			const float hgt = (float)std::atof(r.heightValue.c_str());
			if (hgt != r.height)
			{
				r.height = hgt;
				m_relayout = true; // the rows after it move (RW 0x9853C0)
			}
		}
		if (!r.checkedSent)
		{
			checklistCall(r.path, "SetChecked", { AptValue::boolean(r.item.checked) });
			r.checkedSent = true;
		}
		if (!r.visibilitySent)
		{
			checklistCall(r.path, "SetCheckVisibility", { AptValue::boolean(r.item.importance == 1) });
			r.visibilitySent = true;
		}
		if (!r.ySent)
		{
			checklistCall(r.path, "SetY", { AptValue::number((double)r.y) });
			r.ySent = true;
		}
	}
}

void AptStrategicHUD::clearNotice()
{
	if (m_noticeBox && m_noticeShown)
	{
		m_noticeBox->hide(true); // the notice's release instant-hides the box (RW 0x81A38D(1))
	}
	m_noticeShown = false;
}

void AptStrategicHUD::setRegionStats(const RegionStats &stats)
{
	m_regionStats = stats;
	writeRegionStats(false);
}

// RegionDisplay RW 0x9DAD60 -> the tray: SetVisibility (SetState "_show" / "_hide" on a change, RW tray + 0x68) and per value pair a changed text
// record "APT:<tray>_<Name>" = L"%d/%d", a single space when the total is not positive (StrategicHUD::SetString RW 0x9E25C8)
void AptStrategicHUD::writeRegionStats(bool force)
{
	if (m_regionStatsClip.empty())
	{
		return;
	}
	const std::string base = "_level" + std::to_string(level()) + "." + m_regionStatsClip;
	if (m_regionStats.shown != m_regionStatsShown.shown) // the tray starts hidden (RW 0x9E2C20: + 0x68 false)
	{
		std::string err;
		if (!windows().invokeASAt(level(), m_regionStatsClip, "SetState", { m_regionStats.shown ? "_show" : "_hide" }, nullptr, &err))
		{
			windows().note("invoke-failed", "StrategicPalantir regionStats SetState: " + err);
		}
	}
	static const char *const names[3] = { "BuildPlots", "ArmoryPoints", "CommandPoints" };
	for (int i = 0; i < 3; ++i)
	{
		const int a = m_regionStats.values[i][0], b = m_regionStats.values[i][1];
		if (force || a != m_regionStatsShown.values[i][0] || b != m_regionStatsShown.values[i][1])
		{
			windows().setAptText("APT:" + base + "_" + names[i], b > 0 ? std::to_string(a) + "/" + std::to_string(b) : std::string(" "));
		}
	}
	m_regionStatsShown = m_regionStats;
}

void AptStrategicHUD::showHelp(const std::string &titleLabel, const std::string &helpLabel)
{
	// CommandButtonMovieClipWithHelp::ShowHelp RW 0x9E3089 -> StandardCommandButtonSettings::CreateHelp RW 0x9F466E: fetch(Title), fetch(Help) (an empty
	// label: an empty string), shown at once
	if (!m_helpBox.isLoaded())
	{
		return;
	}
	auto fetch = [this](const std::string &label) { return label.empty() ? UnicodeString() : fetchOrMissing(m_env.gameText, label); };
	m_helpShown = std::make_shared<CommandButtonHelp>(CommandButtonHelp::simple(fetch(titleLabel), fetch(helpLabel)));
	m_helpBox.setHelp(m_helpShown);
	helpUpdate();
}

void AptStrategicHUD::hideHelp()
{
	// the hide guard (RW 0x9E30EA): only the help this source gave, and only soon (5 updates)
	if (m_helpShown && m_helpBox.help() == m_helpShown.get())
	{
		m_helpBox.hideSoon();
	}
	m_helpShown.reset();
}

void AptStrategicHUD::helpUpdate()
{
	if (!m_helpBox.isLoaded())
	{
		return;
	}
	if (!m_helpSettingsTried)
	{
		m_helpSettingsTried = true;
		std::string error;
		if (!m_env.fileSystem || !HelpBoxSettings::load(*m_env.fileSystem, m_helpSettings, &error))
		{
			windows().note("missing-data", "StrategicHUD help box: InGameUI HelpBox fonts: " + (m_env.fileSystem ? error : std::string("no file system")));
		}
	}
	if (!m_helpMetrics || !m_helpSettings.loaded)
	{
		return;
	}
	// stage units throughout (the render callback's rectangle and the shell's text scale): font scale 1, the y factor 1
	m_helpBox.update([this](CommandButtonHelp &h, int width) { return h.setWidthAndComputeHeight(width, m_helpSettings, 1.0f, 1.0f, 1.0f, *m_helpMetrics); }, 1.0f);
}

void AptStrategicHUD::setCommandButtons(const std::vector<CommandButton> &slots)
{
	for (int i = 1; i <= 5; ++i)
	{
		CommandButton b = (size_t)(i - 1) < slots.size() ? slots[(size_t)(i - 1)] : CommandButton();
		// a button named by its settings block (strategichud.ini: CancelArmyMoveButton, DestroyBuildingButton, ...; "<block>:<argument>") takes the
		// block's Image / Title / Help (StandardCommandButtonSettings, parse table RW 0xC90008) unless the host gave its own
		const std::string block = b.id.substr(0, b.id.find(':'));
		const StrategicHudButton *data = (!b.id.empty() && m_env.livingWorld) ? m_env.livingWorld->strategicHud.button(block) : nullptr;
		if (data)
		{
			b.image = b.image.empty() ? data->image : b.image;
			b.title = b.title.empty() ? data->title : b.title;
			b.help = b.help.empty() ? data->help : b.help;
		}
		m_slots[i].want = b;
	}
	updateCommandSlots();
}

void AptStrategicHUD::updateCommandSlots()
{
	const std::string lp = "_level" + std::to_string(level()) + ".";
	for (int i = 1; i <= 5; ++i)
	{
		CommandSlot &slot = m_slots[i];
		const std::string &frame = m_commandFrames[i];
		if (frame.empty() || !m_parts.count("HelpBox"))
		{
			continue;
		}
		std::string error;
		if (slot.want.id.empty())
		{
			// clearSlot RW 0x9E1C45: the slot's button released (its frame's Hide, RW 0x9D2F21, runs only for a frame shown through the sub menu: never
			// for these buttons). INFERENCE (S-1334): the released button's content leaves the frame: libInGameUI's MovieClipFrame.DeleteContent; the next
			// button creates its content again
			if (slot.created)
			{
				if (!windows().invokeASAt(level(), frame, "DeleteContent", {}, nullptr, &error))
				{
					windows().note("invoke-failed", "StrategicHUD command slot " + std::to_string(i) + " DeleteContent: " + error);
				}
				const CommandButton want = slot.want;
				slot = CommandSlot();
				slot.want = want;
			}
			continue;
		}
		if (!slot.created)
		{
			// CreateContent("StrategicCommandButton", "button") on the slot's frame (RW 0x9D26D2), its callbacks (RW 0x9F3FE1 / 0x9F3C24 / 0x9F3C60 / 0x9F3C6A)
			bool written = false;
			const std::string contentKey = lp + frame + "_ContentName";
			windows().registerProvider(contentKey, [&written](const std::string &, std::string &, bool setting) {
				written = written || setting;
				return setting;
			});
			const bool ok = windows().invokeASAt(level(), frame, "CreateContent", { "StrategicCommandButton", "button" }, nullptr, &error);
			windows().unregisterProvider(contentKey);
			if (!ok || !written)
			{
				windows().note("invoke-failed", "StrategicHUD command slot " + std::to_string(i) + " CreateContent: " + (ok ? std::string("no content name written") : error));
				continue;
			}
			slot.created = true;
			slot.shown = true;
			slot.enabledShown = true;
			slot.imageShown.clear();
			const std::string key = lp + frame + ".button";
			registerCommand(key + "_OnInitialized", [this, i](const std::string &) {
				CommandSlot &s = m_slots[i];
				if (!s.created || s.initialized)
				{
					return;
				}
				s.initialized = true; // OnInitialized: Enable(false) when disabled (below, through the update)
				updateCommandSlots();
			});
			registerCommand(key + "_OnClicked", [this, i](const std::string &) {
				// OnClicked RW 0x9F3C24: the left button on an enabled button is the listener's OnLeftClicked; the others only mark the click
				const CommandSlot &s = m_slots[i];
				m_log.push_back("CommandButton" + std::to_string(i) + "_OnClicked(" + s.want.id + ")");
				if (windows().pressButton() == 0 && s.enabledShown && !s.want.id.empty())
				{
					m_requests.push_back("command:" + s.want.id);
				}
			});
			registerCommand(key + "_OnRollOver", [this, i](const std::string &) { showHelp(m_slots[i].want.title, m_slots[i].want.help); });
			registerCommand(key + "_OnRollOut", [this](const std::string &) { hideHelp(); });
			for (const char *c : { "_OnInitialized", "_OnClicked", "_OnRollOver", "_OnRollOut" })
			{
				m_partCommands["Palantir"].push_back(key + c);
			}
		}

		if (!slot.initialized)
		{
			continue;
		}
		const std::string key = lp + frame + ".button";
		if (slot.want.image != slot.imageShown)
		{
			windows().setAptImage(key + "_Image", slot.want.image); // SetImage RW 0x9F3D56: the content's image map
			slot.imageShown = slot.want.image;
		}
		if (slot.want.enabled != slot.enabledShown &&
			windows().invokeASAtValues(level(), frame + ".button", "Enable", { AptValue::boolean(slot.want.enabled) }, &error)) // SetEnabled RW 0x9F3DD2
		{
			slot.enabledShown = slot.want.enabled;
		}
	}
}

void AptStrategicHUD::updateToggleSlot()
{
	const std::string &frame = m_commandFrames[0];
	if (m_toggle.initialized)
	{
		// RW 0x9DB947: enabled while the tray is enabled and open or closed
		const bool enabled = m_parts.count("SelectionDetails") && m_detailsEnabled && (m_detailsState == 2 || m_detailsState == 0);
		const std::string state = enabled ? "_up" : "_disabled";
		std::string err;
		if (state != m_toggle.state && windows().invokeASAt(level(), frame + ".content", "SetState", { state }, nullptr, &err))
		{
			m_toggle.state = state;
		}
		return;
	}
	if (frame.empty() || m_toggle.created || !m_parts.count("HelpBox"))
	{
		return;
	}
	const StrategicHudButton *data = m_env.livingWorld ? m_env.livingWorld->strategicHud.button("ToggleSelectionDetailsButton") : nullptr;
	if (!data)
	{
		windows().note("missing-data", "StrategicHUD: no ToggleSelectionDetailsButton block (strategichud.ini) [S-1334]");
		return;
	}
	const std::string lp = "_level" + std::to_string(level()) + ".";
	bool written = false;
	const std::string contentKey = lp + frame + "_ContentName";
	windows().registerProvider(contentKey, [&written](const std::string &, std::string &, bool setting) {
		written = written || setting;
		return setting;
	});
	std::string error;
	const bool ok = windows().invokeASAt(level(), frame, "CreateContent", { "CommandButton", "content" }, nullptr, &error);
	windows().unregisterProvider(contentKey);
	if (!ok || !written)
	{
		windows().note("invoke-failed", "StrategicHUD command slot 0 CreateContent: " + (ok ? std::string("no content name written") : error));
		return;
	}
	m_toggle.created = true;
	const std::string key = lp + frame + ".content";
	const std::string image = data->image;
	registerCommand(key + "_OnInitialized", [this, frame, key, image](const std::string &) {
		if (!m_toggle.created || m_toggle.initialized)
		{
			return;
		}
		std::string err;
		if (!windows().invokeASAt(level(), frame + ".content", "SetState", { "_up" }, nullptr, &err))
		{
			windows().note("invoke-failed", "StrategicHUD toggle details SetState: " + err);
			return;
		}
		windows().setAptImage(key + "_Image", image); // the content's RenderImage (`_imageMap` = "<content>_Image")
		windows().setAptText("APT:" + key + "_ProductionCount", " "); // no count on this button (as the in-game palantir's empty slots, AptPalantir::syncFrames)
		m_toggle.initialized = true;
		m_toggle.state = "_up";
		m_log.push_back("ToggleSelectionDetails shown");
		updateCommandUI();
	});
	registerCommand(key + "_OnPress", [this](const std::string &) {
		// RW 0x9DB97E: an enabled tray closes when open and opens when closed (an opening / closing tray ignores the press)
		m_log.push_back("ToggleSelectionDetails pressed");
		if (!m_parts.count("SelectionDetails") || !m_detailsEnabled)
		{
			return;
		}
		if (m_detailsState == 2)
		{
			openDetailsTray(false);
		}
		else if (m_detailsState == 0)
		{
			openDetailsTray(true);
		}
	});
	m_partCommands["Palantir"].push_back(key + "_OnInitialized");
	m_partCommands["Palantir"].push_back(key + "_OnPress");
}

// RW 0x97EE63's tray half: the first update that finds the tray enables and opens it; later updates enable a disabled tray
void AptStrategicHUD::updateDetailsTray()
{
	if (!m_parts.count("SelectionDetails"))
	{
		return;
	}
	if (!m_detailsSeen)
	{
		m_detailsSeen = true;
		m_detailsEnabled = true; // vtable + 4 (true)
		openDetailsTray(true);   // vtable + 0x1C
		return;
	}
	m_detailsEnabled = true; // RW 0x97EE63: !slot 0 -> slot 1 (true)
}

void AptStrategicHUD::openDetailsTray(bool open)
{
	auto it = m_parts.find("SelectionDetails");
	if (it == m_parts.end())
	{
		return;
	}
	std::string error;
	if (!windows().invokeASAt(level(), relativePath(it->second), open ? "Open" : "Close", {}, nullptr, &error)) // RW 0x985B28 / 0x985B5C
	{
		windows().note("invoke-failed", std::string("StrategicDetailsTray ") + (open ? "Open" : "Close") + ": " + error);
		return;
	}
	m_detailsState = open ? 1 : 3;
	m_log.push_back(open ? "SelectionDetails Open" : "SelectionDetails Close");
}

namespace
{
const char *const kRegionPageIds[3] = { "territory", "armies", "structures" }; // WB 0x1FCB2EC
int regionPageSlot(const std::string &id)
{
	for (int i = 0; i < 3; ++i)
	{
		if (id == kRegionPageIds[i])
		{
			return i;
		}
	}
	return -1;
}
} // namespace

void AptStrategicHUD::showRegionPanel(const RegionPanel &panel)
{
	m_region = panel;
	// RegionDetailsPanel (WB 0x15FA950): the first page, structures disabled on a region that is not the local player's
	m_regionPageEnabled[0] = true;
	m_regionPageEnabled[1] = true;
	m_regionPageEnabled[2] = panel.owned;
	int first = panel.firstPage < 0 || panel.firstPage > 2 ? 0 : panel.firstPage;
	if (first == 2 && !panel.owned)
	{
		first = 0;
	}
	m_regionPage = first;
	m_trayWanted = TRAY_REGION;
	syncTrayContent();
	if (m_trayLoaded == TRAY_REGION)
	{
		applyRegionPanel();
	}
}

void AptStrategicHUD::hideRegionPanel()
{
	if (m_trayWanted == TRAY_REGION)
	{
		m_trayWanted = TRAY_NONE;
		syncTrayContent();
	}
}

void AptStrategicHUD::showBuildQueuePanel(const BuildQueuePanel &panel)
{
	const bool fresh = m_trayWanted != TRAY_BUILD_QUEUE;
	m_queue = panel;
	if (fresh || m_queueSelected >= (int)panel.queue.size())
	{
		m_queueSelected = -1; // a new panel starts with nothing selected (Impl + 0x30 = -1, WB 0x1644080)
	}
	m_trayWanted = TRAY_BUILD_QUEUE;
	syncTrayContent();
	if (m_trayLoaded == TRAY_BUILD_QUEUE)
	{
		applyBuildQueuePanel();
	}
}

void AptStrategicHUD::hideBuildQueuePanel()
{
	if (m_trayWanted == TRAY_BUILD_QUEUE)
	{
		m_trayWanted = TRAY_NONE;
		syncTrayContent();
	}
}

namespace
{
const char *trayMovie(int kind)
{
	// RW 0x9DBC02's movie / WB 0x1645A60 / the army details factories' WB 0x1639870
	return kind == 1 ? "StrategicDetailsRegion.swf" : (kind == 2 ? "StrategicDetailsBuildQueue.swf" : "StrategicDetailsArmyRetinue.swf");
}
} // namespace

// SelectionDetailsPanelFrame::Impl::SetPanel (WB 0x15E1450): a different panel unloads the frame's movie (ClearPanel) and loads its own
void AptStrategicHUD::syncTrayContent()
{
	if (m_trayLoaded != TRAY_NONE && m_trayLoaded != m_trayWanted)
	{
		unloadTrayContent();
	}
	if (m_trayWanted == TRAY_NONE || m_trayLoaded != TRAY_NONE || m_trayLoading != TRAY_NONE || m_panelFrame.empty())
	{
		return; // loaded when the frame reports itself (_OnPanelFrameLoaded) or the pending movie arrives
	}
	std::string error;
	if (!windows().invokeASAt(level(), m_panelFrame, "LoadContent", { trayMovie(m_trayWanted) }, nullptr, &error))
	{
		windows().note("invoke-failed", "StrategicHUD panel frame LoadContent: " + error);
		return;
	}
	m_trayLoading = m_trayWanted;
	m_log.push_back(std::string("PanelFrame.LoadContent(") + trayMovie(m_trayWanted) + ")");
}

void AptStrategicHUD::unloadTrayContent()
{
	for (const std::string &name : m_regionCommands)
	{
		windows().unregisterCommand(name);
	}
	m_regionCommands.clear();
	for (int i = 0; i < 3; ++i)
	{
		m_regionPages[i].clear();
		m_regionPageShown[i] = false;
	}
	m_regionClip.clear();
	for (const std::string &name : m_queueCommands)
	{
		windows().unregisterCommand(name);
	}
	m_queueCommands.clear();
	m_queueClip.clear();
	releaseArmyView(m_armyView);
	if (m_trayLoaded != TRAY_NONE && !m_panelFrame.empty())
	{
		std::string error;
		if (!windows().invokeASAt(level(), m_panelFrame, "UnloadContent", {}, nullptr, &error))
		{
			windows().note("invoke-failed", "StrategicHUD panel frame UnloadContent: " + error);
		}
	}
	m_trayLoaded = TRAY_NONE;
	m_trayClip.clear();
}

void AptStrategicHUD::onTrayContentLoaded(const std::string &clip)
{
	m_log.push_back("PanelFrame_OnContentLoaded(" + clip + ")");
	const int kind = m_trayLoading;
	m_trayLoading = TRAY_NONE;
	if (kind == TRAY_NONE || m_trayLoaded != TRAY_NONE)
	{
		return;
	}
	m_trayLoaded = kind;
	m_trayClip = relativePath(clip);
	if (kind != m_trayWanted)
	{
		syncTrayContent(); // the selection changed while the movie loaded: unload it, load the wanted one
		return;
	}
	if (kind == TRAY_REGION)
	{
		onRegionContentLoaded();
	}
	else if (kind == TRAY_BUILD_QUEUE)
	{
		onBuildQueueContentLoaded();
	}
	else
	{
		onArmyContentLoaded(m_armyView, m_trayClip);
	}
}

void AptStrategicHUD::onRegionContentLoaded()
{
	m_regionClip = m_trayClip;
	m_armiesPageBound = false;
	m_armySlotsSent = -1;
	m_structuresPageBound = false;
	m_plotSlotsSent = -1;
	m_fortressStateSent.clear();
	m_buildingNameStateSent.clear();
	m_armyNameStateSent.clear();
	m_armyCpStateSent.clear();
	m_regionTabsSent.clear();
	for (int &e : m_regionTabEnabledSent)
	{
		e = -1;
	}
	const std::string base = "_level" + std::to_string(level()) + "." + m_regionClip;
	auto add = [&](const std::string &suffix, WindowManager::CommandFn fn) {
		if (registerCommand(base + suffix, std::move(fn)))
		{
			m_regionCommands.push_back(base + suffix);
		}
	};
	add("_OnPageLoaded", [this](const std::string &arg) { // WB 0x1635060: "id=<page>&name=<clip>"
		const int slot = regionPageSlot(queryValueOf(arg, "id"));
		if (slot < 0)
		{
			windows().note("bad-argument", "StrategicDetailsRegion _OnPageLoaded: " + arg);
			return;
		}
		m_regionPages[slot] = relativePath(queryValueOf(arg, "name"));
		m_regionPageShown[slot] = false;
		applyRegionPanel();
	});
	add("_OnPageUnloaded", [this](const std::string &arg) { // WB 0x1635380
		const int slot = regionPageSlot(queryValueOf(arg, "id"));
		if (slot >= 0)
		{
			m_regionPages[slot].clear();
			m_regionPageShown[slot] = false;
		}
	});
	add("_OnTabClicked", [this](const std::string &arg) { // the tab clip's name (StrategicDetailsRegion.swf); RegionDetailsPanel::OnCurrentPageChanged
		const int slot = regionPageSlot(arg);
		if (slot < 0 || !m_regionPageEnabled[slot] || slot == m_regionPage)
		{
			return;
		}
		m_regionPage = slot;
		applyRegionPanel();
	});
	applyRegionPanel();
}

// RegionDetailsMovieClip: the tabs' enabled states, the current tab, the current page shown and the others hidden (WB 0x16344C0 / 0x1634D10 /
// 0x1634620 / 0x16349D0); the territory page's texts
void AptStrategicHUD::applyRegionPanel()
{
	if (m_regionClip.empty())
	{
		return;
	}
	// SetPageEnabled / SetCurrentPage send only changes (WB 0x16344C0 / 0x1634E80): the movie's gotoAndPlay of the label it is already on would set the
	// tab clips playing past their stop (the native does nothing but set the play flag on the current frame, RW 0x00AE2C60)
	std::string error;
	for (int i = 0; i < 3; ++i)
	{
		const int enabled = m_regionPageEnabled[i] ? 1 : 0;
		if (enabled != m_regionTabEnabledSent[i] &&
			windows().invokeASAtValues(level(), m_regionClip, "SetTabEnabled", { AptValue::string(kRegionPageIds[i]), AptValue::boolean(enabled != 0) }, &error))
		{
			m_regionTabEnabledSent[i] = enabled;
		}
	}
	const std::string tabs = m_regionPage < 0 ? std::string("_hide") : std::string("_") + kRegionPageIds[m_regionPage];
	if (tabs != m_regionTabsSent && windows().invokeASAt(level(), m_regionClip, "SetTabsState", { tabs }, nullptr, &error))
	{
		m_regionTabsSent = tabs;
	}
	for (int i = 0; i < 3; ++i)
	{
		if (m_regionPages[i].empty())
		{
			continue;
		}
		const bool show = i == m_regionPage;
		if (show != m_regionPageShown[i])
		{
			if (windows().invokeASAtValues(level(), m_regionClip, "SetPageVisibility", { AptValue::string(kRegionPageIds[i]), AptValue::boolean(show) }, &error))
			{
				m_regionPageShown[i] = show;
			}
			else
			{
				windows().note("invoke-failed", "StrategicDetailsRegion SetPageVisibility: " + error);
			}
		}
	}
	fillTerritoryPage();
	fillArmiesPage();
	fillStructuresPage();
}

// RegionDetailsArmiesMovieClip (WB 0x1656EE0 SetIconSlotCount, 0x16562E0 SetIconSlotState: 0 _empty, 1 _up, 2 _selected (WB 0x1FD1D60), 0x16571C0 /
// 0x16573B0 SetArmyNameState / SetCommandPointsState "_show" / "_hide" with "APT:_level%u.%s_ArmyName" / _CommandPoints (STRATEGICHUD:StatsCommandPoints));
// the slots are the movie's attached IconSlot clips <page>.slots.<i>, their RenderImage records <page> + "_IconSlotPortrait" / "_IconSlotTypeImage"
// + the slot's name; a slot's click is <page>_OnIconSlotClicked<slot name> (the page's left click selects the army, WB 0x162FCB0)
// INFERENCE (S-1334): the name / command point lines show the selected army (retail's caller of ShowArmyName was not read); the help box of a
// hovered slot (StrategicInGameUI::CreateHelpFor) is not ported
void AptStrategicHUD::fillArmiesPage()
{
	const std::string &page = m_regionPages[1];
	if (page.empty())
	{
		return;
	}
	const std::string lv = "_level" + std::to_string(level()) + ".";
	if (!m_armiesPageBound)
	{
		m_armiesPageBound = true;
		auto add = [&](const std::string &suffix, WindowManager::CommandFn fn) {
			if (registerCommand(lv + page + suffix, std::move(fn)))
			{
				m_regionCommands.push_back(lv + page + suffix);
			}
		};
		// each icon slot registers its own commands (IconSlot WB 0x1655C10): <page>_OnIconSlotClicked<slot>, _OnIconSlotRollOver<slot>, _OnIconSlotRollOut<slot>
		for (int slot = 0; slot < 5; ++slot)
		{
			const std::string n = std::to_string(slot);
			add("_OnIconSlotClicked" + n, [this, slot](const std::string &) {
				m_log.push_back("ArmiesPage_OnIconSlotClicked(" + std::to_string(slot) + ")");
				if (slot < (int)m_region.armies.size())
				{
					m_requests.push_back("select_army:" + std::to_string(m_region.armies[(size_t)slot].id));
				}
			});
			add("_OnIconSlotRollOver" + n, [this, n](const std::string &) { m_log.push_back("ArmiesPage_OnIconSlotRollOver(" + n + ")"); }); // the help box: not ported
			add("_OnIconSlotRollOut" + n, [this, n](const std::string &) { m_log.push_back("ArmiesPage_OnIconSlotRollOut(" + n + ")"); });
		}
		add("_OnRollOverCommandPoints", [this](const std::string &arg) { m_log.push_back("ArmiesPage_OnRollOverCommandPoints(" + arg + ")"); });
		add("_OnRollOutCommandPoints", [this](const std::string &arg) { m_log.push_back("ArmiesPage_OnRollOutCommandPoints(" + arg + ")"); });
	}
	std::string error;
	const int n = (int)std::min<size_t>(m_region.armies.size(), 5);
	if (n != m_armySlotsSent)
	{
		if (!windows().invokeASAtValues(level(), page, "SetIconSlotCount", { AptValue::integer(n) }, &error))
		{
			windows().note("invoke-failed", "StrategicDetailsArmies SetIconSlotCount: " + error);
			return;
		}
		m_armySlotsSent = n;
		for (std::string &st : m_armySlotStateSent)
		{
			st.clear();
		}
	}
	const RegionPanel::Army *selected = nullptr;
	for (int i = 0; i < n; ++i)
	{
		const RegionPanel::Army &a = m_region.armies[(size_t)i];
		const bool isSelected = a.id == m_region.selectedArmy;
		if (isSelected)
		{
			selected = &a;
		}
		const std::string state = isSelected ? "_selected" : "_up";
		if (state != m_armySlotStateSent[i] &&
			windows().invokeASAtValues(level(), page, "SetIconSlotState", { AptValue::integer(i), AptValue::string(state) }, &error))
		{
			m_armySlotStateSent[i] = state;
		}
		windows().setAptImage(lv + page + "_IconSlotPortrait" + std::to_string(i), a.portrait);
		windows().setAptImage(lv + page + "_IconSlotTypeImage" + std::to_string(i), a.typeImage);
	}
	const std::string nameState = selected ? "_show" : "_hide";
	if (selected)
	{
		windows().setAptText("APT:" + lv + page + "_ArmyName", selected->name);
		const std::string cp = loadScreenU16ToUtf8(LivingWorldSetupText::format(fetchOrMissing(m_env.gameText, "STRATEGICHUD:StatsCommandPoints"),
			{ LivingWorldSetupText::FormatArg(selected->commandPoints), LivingWorldSetupText::FormatArg(selected->maxCommandPoints) }));
		windows().setAptText("APT:" + lv + page + "_CommandPoints", cp);
		m_statRecords["APT:" + lv + page + "_ArmyName"] = selected->name;
		m_statRecords["APT:" + lv + page + "_CommandPoints"] = cp;
	}
	if (nameState != m_armyNameStateSent && windows().invokeASAt(level(), page, "SetArmyNameState", { nameState }, nullptr, &error))
	{
		m_armyNameStateSent = nameState;
	}
	if (nameState != m_armyCpStateSent && windows().invokeASAt(level(), page, "SetCommandPointsState", { nameState }, nullptr, &error))
	{
		m_armyCpStateSent = nameState;
	}
}

// RegionDetailsStructuresMovieClip / RegionDetailsStructuresPage (WorldBuilder twins 0x1631620 ..): one icon slot per build plot (SetIconSlotCount;
// SetIconSlotState _empty / _up / _selected), a standing building's ConstructButtonImage and its type's StrategicHUD TypeImages image under the movie's
// records (<page>_IconSlotPortrait<n> / _IconSlotTypeImage<n>, as the armies page's), a building under construction with SetIconSlotProgressState("_show")
// and "APT:_level%u.%s_IconSlotTurnsRemaining%d" (STRATEGICHUD:ConstructionOneTurnRemaining / ConstructionTurnsRemaining), the region's fortress icon
// (SetRegionFortressIconState: CreateAutoFort, its FortressPortrait and the BuildingFortress type image), the selected plot's building name
// (SetBuildingNameState, <page>_BuildingName); a slot's click (<page>_OnIconSlotClicked<n>) asks to select the plot.
// INFERENCE (S-1334): an empty plot's slot state (_empty) and the turns text's argument; the progress drawing (_RenderProgress) is not ported
void AptStrategicHUD::fillStructuresPage()
{
	const std::string &page = m_regionPages[2];
	if (page.empty())
	{
		return;
	}
	const std::string lv = "_level" + std::to_string(level()) + ".";
	if (!m_structuresPageBound)
	{
		m_structuresPageBound = true;
		for (int slot = 0; slot < 8; ++slot)
		{
			const std::string n = std::to_string(slot);
			const std::string name = lv + page + "_OnIconSlotClicked" + n;
			if (registerCommand(name, [this, slot](const std::string &) {
					m_log.push_back("StructuresPage_OnIconSlotClicked(" + std::to_string(slot) + ")");
					if (slot < (int)m_region.plots.size())
					{
						m_requests.push_back("select_plot:" + std::to_string(slot));
					}
				}))
			{
				m_regionCommands.push_back(name);
			}
			for (const char *other : { "_OnIconSlotRollOver", "_OnIconSlotRollOut", "_OnIconSlotTypeRollOver", "_OnIconSlotTypeRollOut" })
			{
				const std::string cmd = lv + page + other + n;
				if (registerCommand(cmd, [this, cmd](const std::string &) { m_log.push_back(cmd); })) // the help box: not ported
				{
					m_regionCommands.push_back(cmd);
				}
			}
		}
		for (const char *other : { "_OnRegionFortressRollOver", "_OnRegionFortressRollOut", "_OnRegionFortressTypeRollOver", "_OnRegionFortressTypeRollOut",
				 "_OnRollOverTurnsRemaining", "_OnRollOutTurnsRemaining" })
		{
			const std::string cmd = lv + page + other;
			if (registerCommand(cmd, [this, cmd](const std::string &) { m_log.push_back(cmd); }))
			{
				m_regionCommands.push_back(cmd);
			}
		}
	}
	const StrategicHudData *hudData = m_env.livingWorld ? &m_env.livingWorld->strategicHud : nullptr;
	auto typeImage = [&](int type) -> std::string {
		static const char *const kTypes[5] = { "", "BuildingFortress", "BuildingArmory", "BuildingResource", "BuildingBarracks" }; // RW 0xC914BC
		if (!hudData || type < 1 || type > 4)
		{
			return std::string();
		}
		auto it = hudData->typeImages.find(kTypes[type]);
		return it == hudData->typeImages.end() ? std::string() : it->second;
	};
	std::string error;
	const int n = (int)std::min<size_t>(m_region.plots.size(), 8);
	if (n != m_plotSlotsSent)
	{
		if (!windows().invokeASAtValues(level(), page, "SetIconSlotCount", { AptValue::integer(n) }, &error))
		{
			windows().note("invoke-failed", "StrategicDetailsStructures SetIconSlotCount: " + error);
			return;
		}
		m_plotSlotsSent = n;
		for (int i = 0; i < 8; ++i)
		{
			m_plotSlotStateSent[i].clear();
			m_plotProgressSent[i].clear();
		}
	}
	for (int i = 0; i < n; ++i)
	{
		const RegionPanel::Plot &plot = m_region.plots[(size_t)i];
		const std::string state = !plot.built ? "_empty" : (i == m_region.selectedPlot ? "_selected" : "_up");
		if (state != m_plotSlotStateSent[i] &&
			windows().invokeASAtValues(level(), page, "SetIconSlotState", { AptValue::integer(i), AptValue::string(state) }, &error))
		{
			m_plotSlotStateSent[i] = state;
		}
		windows().setAptImage(lv + page + "_IconSlotPortrait" + std::to_string(i), plot.built ? plot.portrait : std::string());
		windows().setAptImage(lv + page + "_IconSlotTypeImage" + std::to_string(i), plot.built ? typeImage(plot.type) : std::string());
		const std::string progress = plot.built && plot.turnsLeft > 0 ? "_show" : "_hide";
		if (progress != m_plotProgressSent[i] &&
			windows().invokeASAtValues(level(), page, "SetIconSlotProgressState", { AptValue::integer(i), AptValue::string(progress) }, &error))
		{
			m_plotProgressSent[i] = progress;
		}
		if (plot.built && plot.turnsLeft > 0)
		{
			const std::string turns = plot.turnsLeft == 1 ? text("STRATEGICHUD:ConstructionOneTurnRemaining")
														  : formatOne(text("STRATEGICHUD:ConstructionTurnsRemaining"), std::to_string(plot.turnsLeft), 0.0, false);
			const std::string record = "APT:" + lv + page + "_IconSlotTurnsRemaining" + std::to_string(i);
			windows().setAptText(record, turns);
			m_statRecords[record] = turns;
		}
	}
	const std::string fortressState = m_region.fortress ? "_show" : "_hide";
	if (fortressState != m_fortressStateSent && windows().invokeASAt(level(), page, "SetRegionFortressIconState", { fortressState }, nullptr, &error))
	{
		m_fortressStateSent = fortressState;
	}
	if (m_region.fortress)
	{
		windows().setAptImage(lv + page + "_RegionFortressPortrait", m_region.fortressPortrait);
		windows().setAptImage(lv + page + "_RegionFortressTypeImage", typeImage(1));
	}
	const bool named = m_region.selectedPlot >= 0 && m_region.selectedPlot < n;
	if (named)
	{
		windows().setAptText("APT:" + lv + page + "_BuildingName", m_region.selectedPlotName);
	}
	const std::string nameState = named ? "_show" : "_hide";
	if (nameState != m_buildingNameStateSent && windows().invokeASAt(level(), page, "SetBuildingNameState", { nameState }, nullptr, &error))
	{
		m_buildingNameStateSent = nameState;
	}
}

// StrategicInGameUI::BuildQueueDetailsPanel (WB 0x1644080 ..) and its movie clip StrategicHUD::BuildQueueDetailsMovieClip (WB 0x166F700 ..): one
// in-progress icon slot (queue index 0) and seven queued slots (0 .. 6 = queue index 1 .. 7, NUM_QUEUED_ICON_SLOTS WB 0x1FCD7D0); slot states
// _empty / _up / _selected (WB 0x1FD5C20) through SetInProgressIconSlotState / SetQueuedIconSlotState; the images under <clip>_InProgressIconSlotPortrait /
// _InProgressIconSlotTypeImage / _QueuedIconSlotPortrait<n> / _QueuedIconSlotTypeImage<n>; the texts _BuildingName, _InProgressIconSlotQuantity /
// _QueuedIconSlotQuantity<n> ("%d", "" when 0), _InProgressIconSlotTurnsRemaining / _QueuedIconSlotTurnsRemaining<n> (FormatNumTurnsString:
// STRATEGICHUD:ConstructionOneTurnRemaining for 1, else ConstructionTurnsRemaining); the selected entry's _UnitName, _BuildTime
// (STRATEGICHUD:BuildTimeSingularLabel for 1, BuildTimePluralLabel for n >= 0, "" below) and _CommandPoints (STRATEGICHUD:CommandPointsLabel) with
// SetUnitNameState / SetBuildTimeState / SetCommandPointsState (_show / _hide, sent on a change). A detached slot keeps its turns text (WB 0x1642880).
// The in-progress slot's progress overlay: the engine registers a render callback under "<clip>_ProgressOverlay" (InProgressIconSlot ctor
// WB 0x166D7xx -> 0x1671FC0; RenderProgressOverlay WB 0x166E2F0: (total - remaining) / total of the slot's rectangle in ProgressOverlayColor),
// but no clip of the retail StrategicDetailsBuildQueue.swf names it (its constants hold no "_ProgressOverlay"), so retail never draws it and
// neither does this port. Not ported (S-1334): the help box and the tooltips.
void AptStrategicHUD::onBuildQueueContentLoaded()
{
	m_queueClip = m_trayClip;
	m_queueInProgressSent.clear();
	for (std::string &st : m_queueSlotSent)
	{
		st.clear();
	}
	for (std::string &st : m_queueStatsSent)
	{
		st.clear();
	}
	const std::string base = "_level" + std::to_string(level()) + "." + m_queueClip;
	auto add = [&](const std::string &suffix, WindowManager::CommandFn fn) {
		if (registerCommand(base + suffix, std::move(fn)))
		{
			m_queueCommands.push_back(base + suffix);
		}
	};
	auto logOnly = [&](const std::string &suffix) {
		add(suffix, [this, suffix](const std::string &) { m_log.push_back("BuildQueue" + suffix); }); // the help box / tooltips: not ported
	};
	add("_OnBackButtonClicked", [this](const std::string &) { // WB 0x1670C70: the left button only -> DetailsPanel::SelectRegion (WB 0x15FC7A0)
		m_log.push_back("BuildQueue_OnBackButtonClicked");
		if (windows().pressButton() == 0)
		{
			m_requests.push_back("bq_back");
		}
	});
	logOnly("_OnBackButtonRollOver");
	logOnly("_OnBackButtonRollOut");
	add("_OnInProgressIconSlotClicked", [this](const std::string &) { onQueueSlotClicked(0); });
	for (const char *suffix : { "_OnInProgressIconSlotRollOver", "_OnInProgressIconSlotRollOut", "_OnInProgressIconSlotTurnsRemainingRollOver",
			 "_OnInProgressIconSlotTurnsRemainingRollOut", "_OnInProgressIconSlotTypeRollOver", "_OnInProgressIconSlotTypeRollOut" })
	{
		logOnly(suffix);
	}
	for (int i = 0; i < 7; ++i) // the movie appends the slot clip's _name to each queued slot's command
	{
		const std::string n = std::to_string(i);
		add("_OnQueuedIconSlotClicked" + n, [this, i](const std::string &) { onQueueSlotClicked(i + 1); });
		for (const char *suffix : { "_OnQueuedIconSlotRollOver", "_OnQueuedIconSlotRollOut", "_OnQueuedIconSlotTurnsRemainingRollOver",
				 "_OnQueuedIconSlotTurnsRemainingRollOut", "_OnQueuedIconSlotTypeRollOver", "_OnQueuedIconSlotTypeRollOut" })
		{
			logOnly(suffix + n);
		}
	}
	applyBuildQueuePanel();
}

// the slot's click (InProgressIconSlot::OnClicked WB 0x166E180 / QueuedIconSlot::OnClicked WB 0x166F590): TheAptPlayer + 0x318 0 = the left button
// (Icon::OnBuildQueueIconSlotLeftClicked WB 0x1642FC0: SelectQueueIndex), 2 = the right button (OnBuildQueueIconSlotRightClicked WB 0x1643050:
// MSG_LW_DEQUEUE_UNIT_AT_INDEX with the queue index); a slot without an icon observes nothing
void AptStrategicHUD::onQueueSlotClicked(int index)
{
	m_log.push_back("BuildQueue_OnIconSlotClicked(" + std::to_string(index) + ")");
	if (index >= (int)m_queue.queue.size())
	{
		return;
	}
	const int button = windows().pressButton();
	if (button == 0)
	{
		selectQueueIndex(index);
	}
	else if (button == 2)
	{
		m_requests.push_back("bq_dequeue:" + std::to_string(index));
	}
}

// SelectQueueIndex (WB 0x1644A80): clicking the selected entry deselects it; then ShowUnitStats (WB 0x1644B90)
void AptStrategicHUD::selectQueueIndex(int index)
{
	m_queueSelected = index == m_queueSelected ? -1 : index;
	applyBuildQueuePanel();
}

void AptStrategicHUD::applyBuildQueuePanel()
{
	if (m_queueClip.empty())
	{
		return;
	}
	const std::string lv = "_level" + std::to_string(level()) + ".";
	const std::string base = lv + m_queueClip;
	std::string error;
	auto setText = [&](const std::string &suffix, const std::string &value) {
		windows().setAptText("APT:" + base + suffix, value);
		m_statRecords["APT:" + base + suffix] = value;
	};
	auto turnsText = [&](int turns) { // FormatNumTurnsString
		return turns == 1 ? text("STRATEGICHUD:ConstructionOneTurnRemaining")
						  : formatOne(text("STRATEGICHUD:ConstructionTurnsRemaining"), std::to_string(turns), 0.0, false);
	};
	auto quantityText = [](int q) { return q > 0 ? std::to_string(q) : std::string(); };
	const StrategicHudData *hudData = m_env.livingWorld ? &m_env.livingWorld->strategicHud : nullptr;
	auto unitTypeImage = [&](int type) { return unitTypeImageOf(hudData, type); };
	setText("_BuildingName", m_queue.buildingName);
	// the in-progress slot (InProgressIcon::Init WB 0x1643DB0) and the queued slots (QueuedIcon WB 0x16434D0)
	for (int slot = -1; slot < 7; ++slot)
	{
		const int index = slot + 1;
		const BuildQueuePanel::Entry *e = index < (int)m_queue.queue.size() ? &m_queue.queue[(size_t)index] : nullptr;
		const std::string state = !e ? "_empty" : (index == m_queueSelected ? "_selected" : "_up");
		const std::string n = slot < 0 ? std::string() : std::to_string(slot);
		const std::string prefix = slot < 0 ? "_InProgressIconSlot" : "_QueuedIconSlot";
		std::string &sent = slot < 0 ? m_queueInProgressSent : m_queueSlotSent[slot];
		if (state != sent)
		{
			const bool ok = slot < 0 ? windows().invokeASAt(level(), m_queueClip, "SetInProgressIconSlotState", { state }, nullptr, &error)
									 : windows().invokeASAtValues(level(), m_queueClip, "SetQueuedIconSlotState", { AptValue::integer(slot), AptValue::string(state) }, &error);
			if (ok)
			{
				sent = state;
			}
			else
			{
				windows().note("invoke-failed", "StrategicDetailsBuildQueue " + prefix + " state: " + error);
			}
		}
		windows().setAptImage(base + prefix + "Portrait" + n, e ? e->portrait : std::string());
		windows().setAptImage(base + prefix + "TypeImage" + n, e ? unitTypeImage(e->unitType) : std::string());
		setText(prefix + "Quantity" + n, e ? quantityText(e->quantity) : std::string());
		if (e)
		{
			setText(prefix + "TurnsRemaining" + n, turnsText(e->turns));
		}
	}
	// ShowUnitStats (WB 0x1644B90) through ShowUnitName / ShowBuildTime / ShowCommandPoints (WB 0x1670180 / 0x1670470 / 0x1670870) or the hides
	const BuildQueuePanel::Entry *sel = m_queueSelected >= 0 && m_queueSelected < (int)m_queue.queue.size() ? &m_queue.queue[(size_t)m_queueSelected] : nullptr;
	if (sel)
	{
		setText("_UnitName", sel->name);
		const int t = sel->turns;
		setText("_BuildTime", t == 1 ? text("STRATEGICHUD:BuildTimeSingularLabel")
									: (t >= 0 ? formatOne(text("STRATEGICHUD:BuildTimePluralLabel"), std::to_string(t), 0.0, false) : std::string()));
		setText("_CommandPoints", sel->commandPoints >= 0 ? formatOne(text("STRATEGICHUD:CommandPointsLabel"), std::to_string(sel->commandPoints), 0.0, false)
														  : std::string());
	}
	const char *const functions[3] = { "SetUnitNameState", "SetBuildTimeState", "SetCommandPointsState" };
	const std::string want = sel ? "_show" : "_hide";
	for (int i = 0; i < 3; ++i)
	{
		// a hide is sent only after a show (the movie starts hidden: "if shown")
		if (want != m_queueStatsSent[i] && !(want == "_hide" && m_queueStatsSent[i].empty()) &&
			windows().invokeASAt(level(), m_queueClip, functions[i], { want }, nullptr, &error))
		{
			m_queueStatsSent[i] = want;
		}
	}
}

// ---- the army details panel (StrategicDetailsArmyRetinue.swf) ----
// ArmyDetailsMovieClip (WB 0x165FFA0): SetLayout("_hero") for a hero army only (a garrison makes no call), SetBackButtonVisibility(true) (the planning
// factories), GetUnitIconStageDimensions (the movie writes extern <clip>_UnitIconStageWidth / _UnitIconStageHeight), the rollover texts cleared;
// each member a slot: CreateUnitIconSlot(id) (the movie attaches UnitIconSlot into `slots` and writes extern <clip>_NewSlotWidth / _NewSlotHeight),
// its frame reports <clip>_OnUnitIconSlotLoaded<id>, its content "icon" is ArmyUnitIcon or ArmyHeroIcon (ArmyMemberIconMovieClip WB 0x167CF10);
// slot i at (column * slot width, row * slot height), columns = floor(stage width / slot width) (UpdateIconSlotPositions WB 0x1662DB0), shown
// while its row is inside the stage. Not ported (S-1334): the scroll bar (VerticalScrollBar.swf: the first row stays 0), the help box, the
// type / upgrade tooltips, the upgrade icons, the not-there / disbanding / upgrading overlays (the member moves, disbanding and upgrades)
void AptStrategicHUD::showArmyPanel(const ArmyPanel &panel)
{
	ArmyPanelView &v = m_armyView;
	const bool fresh = m_trayWanted != TRAY_ARMY;
	const bool sameShape = !fresh && panel.members.size() == v.panel.members.size() && panel.heroArmy == v.panel.heroArmy;
	v.panel = panel;
	if (!sameShape)
	{
		// the default selection (WB 0x15FB010 / 0x15FBA40): a garrison selects every member; a hero army selects its leader (SelectLeader WB 0x1640BD0:
		// every other member in the set, the leader icon _selected)
		v.selected.assign(panel.members.size(), true);
		v.leaderSelected = panel.heroArmy;
		v.hovered = -1;
		if (m_trayLoaded == TRAY_ARMY)
		{
			unloadTrayContent(); // another army: the panel is made again
		}
	}
	m_trayWanted = TRAY_ARMY;
	syncTrayContent();
	if (m_trayLoaded == TRAY_ARMY)
	{
		applyArmyPanel(v);
	}
}

void AptStrategicHUD::hideArmyPanel()
{
	if (m_trayWanted == TRAY_ARMY)
	{
		m_trayWanted = TRAY_NONE;
		syncTrayContent();
	}
}

int AptStrategicHUD::armySlotsReady() const
{
	const ArmyPanelView &v = m_armyView;
	int n = 0;
	for (const ArmySlot &s : v.slots)
	{
		n += s.initialized ? 1 : 0;
	}
	return n;
}

void AptStrategicHUD::onArmyContentLoaded(ArmyPanelView &v, const std::string &clip)
{
	v.clip = clip;
	v.slots.clear();
	v.nextSlotId = 0; // the movie clip object's running counter (+ 0x20): a new panel, a new object
	v.leaderStateSent.clear();
	for (std::string &st : v.statsSent)
	{
		st.clear();
	}
	v.stageW = v.stageH = v.slotW = v.slotH = 0.0f;
	const std::string base = "_level" + std::to_string(level()) + "." + v.clip;
	auto add = [&](const std::string &suffix, WindowManager::CommandFn fn) {
		if (registerCommand(base + suffix, std::move(fn)))
		{
			v.commands.push_back(base + suffix);
		}
	};
	auto logOnly = [&](const std::string &suffix) { add(suffix, [this, suffix](const std::string &) { m_log.push_back("ArmyDetails" + suffix); }); };
	auto number = [&](const std::string &suffix, float *out) { // the movie's extern writes (RetrieveIconStageDimensions WB 0x1660C40)
		const std::string name = base + suffix;
		if (windows().registerProvider(name, [out](const std::string &, std::string &value, bool setting) {
				if (setting)
				{
					*out = (float)std::atof(value.c_str());
				}
				else
				{
					value = std::to_string((int)*out);
				}
				return true;
			}))
		{
			v.providers.push_back(name);
		}
	};
	number("_UnitIconStageWidth", &v.stageW);
	number("_UnitIconStageHeight", &v.stageH);
	number("_NewSlotWidth", &v.slotW);
	number("_NewSlotHeight", &v.slotH);
	add("_OnBackButtonClicked", [this, &v](const std::string &) { // WB 0x1663210: the left button only -> DetailsPanel::SelectRegion
		m_log.push_back("ArmyDetails_OnBackButtonClicked");
		if (windows().pressButton() == 0)
		{
			m_requests.push_back("army_back");
		}
	});
	logOnly("_OnBackButtonRollOver");
	logOnly("_OnBackButtonRollOut");
	add("_OnIconListBackgroundClicked", [this, &v](const std::string &) { // WB 0x16632F0: the left button deselects everything
		if (windows().pressButton() != 0)
		{
			return;
		}
		v.selected.assign(v.panel.members.size(), false);
		v.leaderSelected = false;
		applyArmyPanel(v);
	});
	logOnly("_OnScrollBarLoaded");
	logOnly("_OnScrollBarUnloaded");
	add("_OnLeaderIconClicked", [this, &v](const std::string &) { // WB 0x166C6B0: a leader click toggles SelectLeader / DeselectLeader
		if (!v.panel.heroArmy || windows().pressButton() != 0)
		{
			return;
		}
		v.leaderSelected = !v.leaderSelected;
		v.selected.assign(v.panel.members.size(), v.leaderSelected); // SelectLeader puts every member in the set; DeselectLeader empties it
		applyArmyPanel(v);
	});
	logOnly("_OnLeaderIconRollOver");
	logOnly("_OnLeaderIconRollOut");
	logOnly("_OnLeaderTypeRollOver");
	logOnly("_OnLeaderTypeRollOut");
	std::string error;
	if (v.panel.heroArmy && !windows().invokeASAt(level(), v.clip, "SetLayout", { "_hero" }, nullptr, &error))
	{
		windows().note("invoke-failed", "StrategicDetailsArmyRetinue SetLayout: " + error);
	}
	windows().invokeASAtValues(level(), v.clip, "SetBackButtonVisibility", { AptValue::boolean(v.backButton) }, &error);
	windows().invokeASAt(level(), v.clip, "GetUnitIconStageDimensions", {}, nullptr, &error);
	for (const char *t : { "_MemberName", "_MemberRank", "_CommandPoints" })
	{
		windows().setAptText("APT:" + base + t, std::string());
	}
	if (v.panel.heroArmy)
	{
		// HeroArmyDetailsPanel::Impl (WB 0x16402B0): the leader's portrait, type image, quantity 1, rank and progress, set once
		const ArmyPanel::Member &l = v.panel.leader;
		const StrategicHudData *hud = m_env.livingWorld ? &m_env.livingWorld->strategicHud : nullptr;
		windows().setAptImage(base + "_LeaderPortrait", l.portrait);
		windows().setAptImage(base + "_LeaderTypeImage", unitTypeImageOf(hud, l.unitType));
		windows().setAptText("APT:" + base + "_LeaderQuantity", "1");
		m_statRecords["APT:" + base + "_LeaderQuantity"] = "1";
		const std::string rank = formatOne(text("APT:RankLabel"), std::to_string(l.rank), 0.0, false);
		windows().setAptText("APT:" + base + "_LeaderRank", rank);
		m_statRecords["APT:" + base + "_LeaderRank"] = rank;
		if (l.progress >= 0.0f)
		{
			windows().invokeASAtValues(level(), v.clip, "SetLeaderRankProgress", { AptValue::number(l.progress) }, &error);
			windows().invokeASAt(level(), v.clip, "SetLeaderRankProgressBarState", { "_show" }, nullptr, &error);
		}
		else
		{
			windows().invokeASAt(level(), v.clip, "SetLeaderRankProgressBarState", { "_hide" }, nullptr, &error);
		}
	}
	// one slot per member (CreateMemberIconSlot WB 0x16611B0)
	for (size_t i = 0; i < v.panel.members.size(); ++i)
	{
		ArmySlot slot;
		slot.id = v.nextSlotId++;
		const int index = (int)i;
		add("_OnUnitIconSlotLoaded" + std::to_string(slot.id), [this, &v, index](const std::string &frame) { onArmySlotLoaded(v, index, frame); });
		v.slots.push_back(slot);
		if (!windows().invokeASAtValues(level(), v.clip, "CreateUnitIconSlot", { AptValue::integer(slot.id) }, &error))
		{
			windows().note("invoke-failed", "StrategicDetailsArmyRetinue CreateUnitIconSlot: " + error);
		}
	}
	applyArmyPanel(v);
}

void AptStrategicHUD::releaseArmyView(ArmyPanelView &v)
{
	for (const std::string &name : v.commands)
	{
		windows().unregisterCommand(name);
	}
	v.commands.clear();
	for (const std::string &name : v.providers)
	{
		windows().unregisterProvider(name);
	}
	v.providers.clear();
	v.slots.clear();
	v.clip.clear();
}

void AptStrategicHUD::onArmySlotLoaded(ArmyPanelView &v, int index, const std::string &frameArg)
{
	if (index < 0 || index >= (int)v.slots.size())
	{
		return;
	}
	ArmySlot &slot = v.slots[(size_t)index];
	slot.frame = relativePath(frameArg);
	const ArmyPanel::Member &m = v.panel.members[(size_t)index];
	std::string error;
	// the frame movie writes extern <frame>_ContentName when it creates its content: the frame object's own record (AptLoadMovieFrame WB 0x14F47E0)
	const std::string contentName = "_level" + std::to_string(level()) + "." + slot.frame + "_ContentName";
	if (windows().registerProvider(contentName, [](const std::string &, std::string &value, bool setting) {
			if (!setting)
			{
				value.clear();
			}
			return true;
		}))
	{
		v.providers.push_back(contentName);
	}
	if (!windows().invokeASAt(level(), slot.frame, "CreateContent", { m.hero ? "ArmyHeroIcon" : "ArmyUnitIcon", "icon" }, nullptr, &error))
	{
		windows().note("invoke-failed", "UnitIconSlot CreateContent: " + error);
		return;
	}
	slot.icon = slot.frame + ".icon";
	const std::string key = "_level" + std::to_string(level()) + "." + slot.icon;
	auto reg = [&](const std::string &name, WindowManager::CommandFn fn) {
		if (registerCommand(name, std::move(fn)))
		{
			v.commands.push_back(name);
		}
	};
	reg(key + "_OnInitialized", [this, &v, index](const std::string &) {
		if (index < (int)v.slots.size())
		{
			v.slots[(size_t)index].initialized = true;
			applyArmyIcon(v, (size_t)index);
		}
	});
	reg(key + "_OnClicked", [this, &v, index](const std::string &) { onArmyIconClicked(v, index); });
	reg(key + "_OnRollOver", [this, &v, index](const std::string &) { showArmyStats(v, index); });
	reg(key + "_OnRollOut", [this, &v](const std::string &) { showArmyStats(v, -1); });
	for (const char *t : { "_OnTypeRollOver", "_OnTypeRollOut", "_OnUpgradeIconRollOver", "_OnUpgradeIconRollOut" })
	{
		const std::string name = key + t;
		reg(name, [this, name](const std::string &) { m_log.push_back(name); }); // the tooltips: not ported
	}
}

// ArmyUnitIcon / ArmyHeroIcon (WB 0x1666270 / 0x1665AE0, DoUpdate WB 0x1666A80 / 0x1665E60): the portrait, type image and quantity once; the state,
// the overlays (hidden: not ported) and a unit's rank stars and dots (SetRankDisplayState(rank / 5, rank % 5), WB 0x167F0A0)
void AptStrategicHUD::applyArmyIcon(ArmyPanelView &v, size_t index)
{
	ArmySlot &slot = v.slots[index];
	if (!slot.initialized)
	{
		return;
	}
	const ArmyPanel::Member &m = v.panel.members[index];
	const StrategicHudData *hud = m_env.livingWorld ? &m_env.livingWorld->strategicHud : nullptr;
	const std::string key = "_level" + std::to_string(level()) + "." + slot.icon;
	std::string error;
	windows().setAptImage(key + "_Portrait", m.portrait);
	windows().setAptImage(key + "_TypeImage", unitTypeImageOf(hud, m.unitType));
	windows().setAptText("APT:" + key + "_Quantity", std::to_string(m.quantity));
	m_statRecords["APT:" + key + "_Quantity"] = std::to_string(m.quantity);
	const std::string state = (index < v.selected.size() && v.selected[index]) ? "_selected" : "_up";
	if (state != slot.stateSent && windows().invokeASAt(level(), slot.icon, "SetState", { state }, nullptr, &error))
	{
		slot.stateSent = state;
	}
	if (!slot.overlaysSent)
	{
		slot.overlaysSent = true;
		windows().invokeASAt(level(), slot.icon, "SetNotThereOverlayState", { "_hide" }, nullptr, &error);
		windows().invokeASAt(level(), slot.icon, "SetDisbandingOverlayState", { "_hide" }, nullptr, &error);
		if (!m.hero)
		{
			const int rank = m.rank < 0 ? 0 : (m.rank > 10 ? 10 : m.rank);
			windows().invokeASAtValues(level(), slot.icon, "SetRankDisplayState", { AptValue::integer(rank / 5), AptValue::integer(rank % 5) }, &error);
			windows().invokeASAt(level(), slot.icon, "SetUpgradeOverlayState", { "_hide" }, nullptr, &error);
		}
	}
}

// OnIconLeftClicked (WB 0x1637EB0; RotWK RW 0x9F8007): without Ctrl, every other icon is deselected and the clicked one selected, or deselected when it
// was the only one selected; with Ctrl (keyboard modifier bits 4 | 8) it toggles. WOTR2 milestone 4: RotWK leaves the leader icon's flag alone (only
// SelectAll / DeselectAll, RW 0x9F9719 / 0x9F9791, change it): with the leader selected a right click still orders the whole army
void AptStrategicHUD::onArmyIconClicked(ArmyPanelView &v, int index)
{
	if (index < 0 || index >= (int)v.selected.size() || windows().pressButton() != 0)
	{
		return;
	}
	m_log.push_back("ArmyDetails_OnIconClicked(" + std::to_string(index) + ")");
	if (m_ctrlDown && m_ctrlDown())
	{
		v.selected[(size_t)index] = !v.selected[(size_t)index];
		applyArmyPanel(v);
		return;
	}
	int selected = 0;
	for (bool b : v.selected)
	{
		selected += b ? 1 : 0;
	}
	const bool onlyThis = selected == 1 && v.selected[(size_t)index];
	v.selected.assign(v.selected.size(), false);
	v.selected[(size_t)index] = !onlyThis;
	applyArmyPanel(v);
}

// the rollover lines (WB 0x16380B0): the hovered member's name, "Level: %d" (APT:RankLabel), its progress bar (hidden at the top level) and its CP
// (STRATEGICHUD:CommandPointsLabel); with nothing hovered the CP of the selected members (and the selected leader), hidden below 1
void AptStrategicHUD::showArmyStats(ArmyPanelView &v, int hovered)
{
	v.hovered = hovered;
	applyArmyPanel(v);
}

void AptStrategicHUD::applyArmyPanel(ArmyPanelView &v)
{
	if (v.clip.empty())
	{
		return;
	}
	const std::string base = "_level" + std::to_string(level()) + "." + v.clip;
	std::string error;
	// the slots' places (UpdateIconSlotPositions WB 0x1662DB0), sent on a change
	if (v.slotW > 0.0f && v.slotH > 0.0f)
	{
		const int columns = std::max(1, (int)std::floor(v.stageW / v.slotW));
		for (size_t i = 0; i < v.slots.size(); ++i)
		{
			ArmySlot &slot = v.slots[i];
			const int x = (int)((float)((int)i % columns) * v.slotW);
			const int y = (int)((float)((int)i / columns) * v.slotH);
			if (x != slot.x || y != slot.y)
			{
				windows().invokeASAtValues(level(), v.clip, "MoveUnitIconSlot", { AptValue::integer(slot.id), AptValue::integer(x), AptValue::integer(y) }, &error);
				const bool visible = (float)y + v.slotH > 1.0f && (float)y < v.stageH - 1.0f;
				windows().invokeASAtValues(level(), v.clip, "SetUnitIconSlotVisibility", { AptValue::integer(slot.id), AptValue::boolean(visible) }, &error);
				slot.x = x;
				slot.y = y;
			}
		}
	}
	for (size_t i = 0; i < v.slots.size(); ++i)
	{
		applyArmyIcon(v, i);
	}
	if (v.panel.heroArmy)
	{
		const std::string leader = v.leaderSelected ? "_selected" : "_up";
		if (leader != v.leaderStateSent && windows().invokeASAt(level(), v.clip, "SetLeaderIconState", { leader }, nullptr, &error))
		{
			v.leaderStateSent = leader;
		}
	}
	auto setState = [&](int which, const char *fn, bool show) {
		const std::string want = show ? "_show" : "_hide";
		if (want != v.statsSent[which] && !(want == "_hide" && v.statsSent[which].empty()) &&
			windows().invokeASAt(level(), v.clip, fn, { want }, nullptr, &error))
		{
			v.statsSent[which] = want;
		}
	};
	auto setText = [&](const std::string &suffix, const std::string &value) {
		windows().setAptText("APT:" + base + suffix, value);
		m_statRecords["APT:" + base + suffix] = value;
	};
	const ArmyPanel::Member *h = v.hovered >= 0 && v.hovered < (int)v.panel.members.size() ? &v.panel.members[(size_t)v.hovered] : nullptr;
	int cp = 0;
	if (h)
	{
		setText("_MemberName", text(h->name)); // the DisplayName label's text
		setText("_MemberRank", formatOne(text("APT:RankLabel"), std::to_string(h->rank), 0.0, false));
		if (h->progress >= 0.0f)
		{
			windows().invokeASAtValues(level(), v.clip, "SetMemberRankProgress", { AptValue::number(h->progress) }, &error);
		}
		cp = h->commandPoints;
	}
	else
	{
		for (size_t i = 0; i < v.panel.members.size(); ++i)
		{
			cp += (i < v.selected.size() && v.selected[i]) ? v.panel.members[i].commandPoints : 0;
		}
		cp += v.panel.heroArmy && v.leaderSelected ? v.panel.leader.commandPoints : 0;
	}
	setState(0, "SetMemberNameState", h != nullptr);
	setState(1, "SetMemberRankState", h != nullptr);
	setState(2, "SetMemberRankProgressBarState", h != nullptr && h->progress >= 0.0f);
	const bool showCp = h ? true : cp >= 1;
	if (showCp)
	{
		setText("_CommandPoints", formatOne(text("STRATEGICHUD:CommandPointsLabel"), std::to_string(cp), 0.0, false));
	}
	setState(3, "SetCommandPointsState", showCp);
}

void AptStrategicHUD::fillTerritoryPage()
{
	const std::string &page = m_regionPages[0];
	if (page.empty())
	{
		return;
	}
	const std::string prefix = "APT:_level" + std::to_string(level()) + "." + page;
	// RegionDetailsTerritoryPage (WB 0x1635B00): the region's map in the map cache: its name and description ("MissingMap" without one)
	std::string lower = m_region.mapName;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	const std::string key = "maps/" + lower + "/" + lower + ".map";
	const MapCacheEntry *map = nullptr;
	if (m_env.skirmish)
	{
		for (const MapCacheEntry &e : m_env.skirmish->maps())
		{
			if (e.name == key)
			{
				map = &e;
			}
		}
	}
	if (!map)
	{
		windows().note("missing-data", "StrategicHUD territory page: no map cache entry " + key + " [S-1334]");
	}
	// the map's name and description are labels of its map.str (else the game's text), as every map's (SkirmishGameSetup::mapBaseDisplayName)
	auto mapText = [&](const std::u16string &label) -> std::string {
		if (!map || label.empty())
		{
			return std::string();
		}
		const bool dollar = label[0] == u'$';
		std::string ascii;
		for (char16_t c : dollar ? label.substr(1) : label)
		{
			ascii.push_back((char)c);
		}
		std::string file = map->name;
		std::replace(file.begin(), file.end(), '/', '\\');
		const size_t slash = file.rfind('\\');
		std::u16string out;
		if (m_env.gameText && slash != std::string::npos && m_env.gameText->fetchMapLabel(file.substr(0, slash + 1) + "map.str", ascii, out))
		{
			return loadScreenU16ToUtf8(out);
		}
		return text(ascii); // fetchOrMissing: a missing label shows as missing, as every label
	};
	// the map's picture (RegionDetailsTerritoryPage: the map cache's preview, else MissingMap; as AptMapPreview's, RW 0x97640C: the map file without
	// ".map" plus "_pic.tga" when the file system has it): the page's RenderImage reads the record "<page>_MapPreview" ("file:" names a texture file)
	{
		std::string picture = "MissingMap";
		if (map && map->name.size() > 4 && m_env.skirmish && m_env.skirmish->hasFileSystem())
		{
			std::string file = map->name.substr(0, map->name.size() - 4) + "_pic.tga";
			std::replace(file.begin(), file.end(), '/', '\\');
			if (m_env.skirmish->fileExists(file))
			{
				picture = "file:" + file;
			}
		}
		windows().setAptImage("_level" + std::to_string(level()) + "." + page + "_MapPreview", picture);
	}
	windows().setAptText(prefix + "_TerritoryName", mapText(map ? map->displayName : std::u16string()));
	windows().setAptText(prefix + "_TerritoryDescription", mapText(map ? map->description : std::u16string()));
	// the seven bonus lines (WB 0x1635F20 / 0x165D040 / 0x1FD3090)
	static const bool kAbsolute[7] = { true, false, false, false, false, true, true };
	for (int i = 0; i < 7; ++i)
	{
		const int v = m_region.bonus[i];
		std::string line;
		if (v < 1)
		{
			line = text("STRATEGICHUD:RegionBonusNone");
		}
		else
		{
			line = formatOne(text(kAbsolute[i] ? "STRATEGICHUD:RegionAbsoluteBonus" : "STRATEGICHUD:RegionPercentageBonus"), std::to_string(v), 0.0, false);
		}
		const std::string name = prefix + "_Bonus" + std::to_string(i);
		windows().setAptText(name, line);
		m_statRecords[name] = line;
	}
}

void AptStrategicHUD::showRadialMenu(float centerX, float centerY, int displayW, int displayH, float stagePerPixelX, float stagePerPixelY,
	const std::vector<RadialButton> &buttons)
{
	hideRadialMenu();
	if (m_radialStage.empty() || buttons.empty())
	{
		return;
	}
	// the in-game ring's layout (RadialMenuLayout: RW 0x9D62D1 .. 0x9D6521) in display pixels; ButtonShell::Move scales to the stage
	RadialMenuLayout layout;
	layout.setCount((int)buttons.size());
	layout.updateCenter((int)centerX, (int)centerY);
	layout.update(displayW, displayH);
	std::vector<RadialMenuLayout::Rect> rects;
	layout.place(rects);
	std::string error;
	for (size_t i = 0; i < buttons.size(); ++i)
	{
		RadialSlot slot;
		slot.button = buttons[i];
		if (i < rects.size())
		{
			slot.x = (int)(rects[i].x * stagePerPixelX);
			slot.y = (int)(rects[i].y * stagePerPixelY);
			slot.w = (int)(rects[i].w * stagePerPixelX);
			slot.h = (int)(rects[i].h * stagePerPixelY);
		}
		m_radialButtons.push_back(slot);
	}
	for (size_t i = 0; i < m_radialButtons.size(); ++i)
	{
		if (!windows().invokeASAtValues(level(), m_radialStage, "CreateButton", { AptValue::integer((int)i) }, &error))
		{
			windows().note("invoke-failed", "InGameRadialMenuStage CreateButton: " + error);
		}
	}
	m_log.push_back("RadialMenu.Show(" + std::to_string(m_radialButtons.size()) + ")");
}

void AptStrategicHUD::hideRadialMenu()
{
	std::string error;
	for (size_t i = 0; i < m_radialButtons.size(); ++i)
	{
		windows().invokeASAtValues(level(), m_radialStage, "DestroyButton", { AptValue::integer((int)i) }, &error);
	}
	for (const std::string &name : m_radialCommands)
	{
		windows().unregisterCommand(name);
	}
	m_radialCommands.clear();
	m_radialButtons.clear();
}

int AptStrategicHUD::radialButtonsReady() const
{
	int n = 0;
	for (const RadialSlot &s : m_radialButtons)
	{
		n += s.initialized ? 1 : 0;
	}
	return n;
}

// a button's image, production count (the command button's ShowProductionCount: "%d", " " below 1, as RW 0x72981E) and enabled state
void AptStrategicHUD::applyRadialButton(RadialSlot &s)
{
	const std::string content = s.frame + ".bttn";
	const std::string lv = "_level" + std::to_string(level()) + ".";
	std::string err;
	windows().setAptImage(lv + content + "_Image", s.button.image);
	windows().setAptText("APT:" + lv + content + "_ProductionCount", s.button.productionCount > 0 ? std::to_string(s.button.productionCount) : std::string(" "));
	if (s.enabledSent != (s.button.enabled ? 1 : 0))
	{
		windows().invokeASAtValues(level(), content, "Enable", { AptValue::boolean(s.button.enabled) }, &err);
		windows().invokeASAt(level(), content, "SetState", { s.button.enabled ? "_up" : "_disabled" }, nullptr, &err);
		s.enabledSent = s.button.enabled ? 1 : 0;
	}
}

void AptStrategicHUD::updateRadialButtons(const std::vector<RadialButton> &buttons)
{
	if (buttons.size() != m_radialButtons.size())
	{
		return;
	}
	for (size_t i = 0; i < buttons.size(); ++i)
	{
		RadialSlot &s = m_radialButtons[i];
		s.button = buttons[i];
		if (s.initialized)
		{
			applyRadialButton(s);
		}
	}
}

// a button's frame reported itself: StrategicCommandButton in it, then its place (MoveButton) once its content initialized
void AptStrategicHUD::onRadialFrameLoaded(int id, const std::string &frameArg)
{
	if (id < 0 || id >= (int)m_radialButtons.size())
	{
		return;
	}
	RadialSlot &slot = m_radialButtons[(size_t)id];
	slot.frame = relativePath(frameArg);
	std::string error;
	if (!windows().invokeASAt(level(), slot.frame, "CreateContent", { "StrategicCommandButton", "bttn" }, nullptr, &error))
	{
		windows().note("invoke-failed", "radial button CreateContent: " + error);
		return;
	}
	const std::string key = "_level" + std::to_string(level()) + "." + slot.frame + ".bttn";
	auto reg = [&](const std::string &name, WindowManager::CommandFn fn) {
		if (registerCommand(name, std::move(fn)))
		{
			m_radialCommands.push_back(name);
		}
	};
	reg(key + "_OnInitialized", [this, id](const std::string &) {
		if (id >= (int)m_radialButtons.size())
		{
			return;
		}
		RadialSlot &s = m_radialButtons[(size_t)id];
		std::string err;
		applyRadialButton(s);
		windows().invokeASAtValues(level(), m_radialStage, "MoveButton",
			{ AptValue::integer(id), AptValue::integer(s.x), AptValue::integer(s.y), AptValue::integer(s.w), AptValue::integer(s.h) }, &err);
		s.initialized = true;
	});
	reg(key + "_OnClicked", [this, id](const std::string &) {
		m_log.push_back("RadialButton_OnClicked(" + std::to_string(id) + ")");
		if (id < (int)m_radialButtons.size() && m_radialButtons[(size_t)id].button.enabled)
		{
			m_requests.push_back("radial:" + std::to_string(id));
		}
	});
	reg(key + "_OnRollOver", [this, id](const std::string &) { m_log.push_back("RadialButton_OnRollOver(" + std::to_string(id) + ")"); }); // the help box: not ported
	reg(key + "_OnRollOut", [this, id](const std::string &) { m_log.push_back("RadialButton_OnRollOut(" + std::to_string(id) + ")"); });
}

void AptStrategicHUD::setTurn(int turn, int phase)
{
	const bool newTurn = m_haveTurn && turn != m_turn;
	m_turn = turn;
	m_phase = phase;
	m_haveTurn = true;
	writeTurn(newTurn);
}

void AptStrategicHUD::writeTurn(bool newTurn)
{
	updateCommandUI(); // the HUD's update (RW 0x97EE63) runs with each sync here
	if (!m_haveTurn)
	{
		return;
	}
	const std::string lv = "APT:_level" + std::to_string(level()) + ".";
	auto it = m_parts.find("Checklist");
	if (it != m_parts.end())
	{
		const std::string path = relativePath(it->second);
		if (!m_checklistButtonUp)
		{
			// RW 0x985531 -> RW 0x9849F9(true): retail's update sends it on the first frame (the state starts false); here on the first sync that reaches
			// the clip (the HUD has no per-frame update of its own)
			std::string error;
			m_checklistButtonUp = windows().invokeASAt(level(), path, "SetExpandButtonState", { "_up" }, nullptr, &error);
		}
		windows().setAptText(lv + path + "_TurnNumber", std::to_string(m_turn)); // RW 0x9845B4: L"%d"
		m_statRecords[lv + path + "_TurnNumber"] = std::to_string(m_turn);
		// RW 0x98471D: the phase's title from the table RW 0xC865A8 {0 APT:TacticalPhaseTitle, 2 APT:BattlePhaseTitle, 4 APT:RetreatPhaseTitle}; any other
		// phase writes an empty title
		static const struct
		{
			int phase;
			const char *label;
		} kTitles[] = { { 0, "APT:TacticalPhaseTitle" }, { 2, "APT:BattlePhaseTitle" }, { 4, "APT:RetreatPhaseTitle" } };
		std::string title;
		for (const auto &t : kTitles)
		{
			if (t.phase == m_phase)
			{
				title = text(t.label);
			}
		}
		windows().setAptText(lv + path + "_PhaseTitle", title);
		m_statRecords[lv + path + "_PhaseTitle"] = title;
	}
	auto nt = m_parts.find("NewTurnIndicator");
	if (newTurn && nt != m_parts.end())
	{
		// RW 0x9860EA: the indicator's turn number, then its movie function Go (RW 0x92B9C8)
		const std::string path = relativePath(nt->second);
		windows().setAptText(lv + path + "_TurnNumber", std::to_string(m_turn));
		std::string error;
		if (!windows().invokeASAt(level(), path, "Go", {}, nullptr, &error))
		{
			windows().note("invoke-failed", "StrategicNextTurnInd Go: " + error);
		}
		m_log.push_back("NewTurnIndicator.Go(" + std::to_string(m_turn) + ")");
	}
}

void AptStrategicHUD::openBattlePrompt(const std::string &regionName, bool autoResolve, bool realTime, bool retreat, const std::string &ally,
	const std::string &enemy, const std::string &realTimeReason)
{
	m_promptWanted = true;
	m_promptRealTimeReason = realTimeReason;
	m_promptRegion = regionName;
	m_promptAlly = ally;
	m_promptEnemy = enemy;
	m_promptButtons[0] = autoResolve;
	m_promptButtons[1] = realTime;
	m_promptButtons[2] = retreat;
	if (!m_promptPath.empty())
	{
		writePrompt();
		return;
	}
	auto it = m_parts.find("LoadDialogFrame");
	if (it == m_parts.end())
	{
		return; // the frame's Loaded makes the call
	}
	std::string error;
	if (!windows().invokeASAt(level(), relativePath(it->second), "LoadContent", { "StrategicBattlePrompt.swf" }, nullptr, &error)) // RW 0x986404
	{
		windows().note("invoke-failed", "StrategicHUD dialog frame LoadContent: " + error);
	}
	m_log.push_back("DialogFrame.LoadContent(StrategicBattlePrompt.swf)");
}

void AptStrategicHUD::onContentLoaded(const std::string &clip)
{
	if (m_swapWanted && m_swapClip.empty()) // the frame keeps one content callback: the swapper's when it asked last (RW 0x986404)
	{
		onSwapperLoaded(clip);
		return;
	}
	if (!m_promptWanted || !m_promptPath.empty())
	{
		return;
	}
	m_promptPath = relativePath(clip);
	const std::string base = "_level" + std::to_string(level()) + "." + m_promptPath;
	auto add = [&](const std::string &suffix, WindowManager::CommandFn fn) {
		if (registerCommand(base + suffix, std::move(fn)))
		{
			m_promptCommands.push_back(base + suffix);
		}
	};
	for (const char *s : { "_OnAllyTabsLoaded", "_OnAllyTabsUnloaded", "_OnEnemyTabsLoaded", "_OnEnemyTabsUnloaded", "_OnOpen", "_OnClosed" })
	{
		const std::string name = s;
		add(name, [this, name](const std::string &arg) { m_log.push_back("BattlePrompt" + name + "(" + arg + ")"); });
	}
	add("_OnAutoResolveButtonClicked", [this](const std::string &) {
		m_log.push_back("BattlePrompt_OnAutoResolveButtonClicked");
		m_requests.push_back("battle_autoresolve");
	});
	add("_OnRealTimeButtonClicked", [this](const std::string &) {
		m_log.push_back("BattlePrompt_OnRealTimeButtonClicked");
		m_requests.push_back("battle_realtime");
	});
	add("_OnRetreatButtonClicked", [this](const std::string &) {
		m_log.push_back("BattlePrompt_OnRetreatButtonClicked");
		m_requests.push_back("battle_retreat");
	});
	windows().note("unported-component", "StrategicBattlePrompt: the ally / enemy player pages are not filled [S-1334]");
	writePrompt();
}

void AptStrategicHUD::writePrompt()
{
	if (m_promptPath.empty())
	{
		return;
	}
	const std::string record = "APT:_level" + std::to_string(level()) + "." + m_promptPath + "_RegionName"; // RW 0xA0CF53
	windows().setAptText(record, m_promptRegion);
	m_statRecords[record] = m_promptRegion;
	// the player tabs' name records the movie shows ("$<tabs path>_PlayerName") [inference S-1334: the tab objects RW 0xA0D4D3 / 0xA0D5EA were not read]
	const std::string tabs[2] = { "APT:_level" + std::to_string(level()) + "." + m_promptPath + ".main.allyTabs_PlayerName",
		"APT:_level" + std::to_string(level()) + "." + m_promptPath + ".main.enemyTabs_PlayerName" };
	const std::string names[2] = { m_promptAlly, m_promptEnemy };
	for (int i = 0; i < 2; ++i)
	{
		windows().setAptText(tabs[i], names[i]);
		m_statRecords[tabs[i]] = names[i];
	}
	static const char *const kButtons[3] = { "AutoResolve", "RealTime", "Retreat" };
	for (int i = 0; i < 3; ++i)
	{
		std::string error;
		if (!windows().invokeASAt(level(), m_promptPath, "SetButtonState", { kButtons[i], m_promptButtons[i] ? "_up" : "_disabled" }, nullptr, &error)) // RW 0xA0CE42: "_up" (RW 0xC19568) or "_disabled"
		{
			windows().note("invoke-failed", std::string("StrategicBattlePrompt SetButtonState ") + kButtons[i] + ": " + error);
		}
	}
	// WOTR2 milestone 8 [S-1339]: Real Time disabled in a network war until its battles are ported: the reason in the help box (the game's tooltip style)
	if (!m_promptButtons[1] && !m_promptRealTimeReason.empty() && m_helpBox.isLoaded())
	{
		m_promptHelp = std::make_shared<CommandButtonHelp>(CommandButtonHelp::simple(UnicodeString(), utf8ToU16(m_promptRealTimeReason)));
		m_helpBox.setHelp(m_promptHelp);
		helpUpdate();
	}
}

void AptStrategicHUD::setTimeRemaining(int seconds)
{
	if (seconds == m_timeRemaining)
	{
		return;
	}
	auto it = m_parts.find("Checklist");
	if (it == m_parts.end())
	{
		return; // written once the checklist is loaded (the next sync)
	}
	m_timeRemaining = seconds;
	const std::string record = "APT:_level" + std::to_string(level()) + "." + relativePath(it->second) + "_TimeRemaining";
	std::u16string text;
	if (seconds >= 0)
	{
		// RW 0x984640: format(STRATEGICHUD:TurnTimeRemaining, seconds / 60, the separator, seconds % 60); RW 0x98430E reads the locale's time separator
		// (LOCALE_STIME, ":" when it has none): ":" here [INFERENCE S-1339: the locale is not read]
		const std::vector<LivingWorldSetupText::FormatArg> args = { LivingWorldSetupText::FormatArg(seconds / 60), LivingWorldSetupText::FormatArg(u":"),
			LivingWorldSetupText::FormatArg(seconds % 60) };
		text = LivingWorldSetupText::format(fetchOrMissing(m_env.gameText, "STRATEGICHUD:TurnTimeRemaining"), args);
	}
	windows().setAptText(record, loadScreenU16ToUtf8(text));
	m_statRecords[record] = loadScreenU16ToUtf8(text);
}

void AptStrategicHUD::setWaitMessage(bool shown)
{
	if (shown && m_noticeShown)
	{
		shown = false; // a notice owns the box until its OK (the wait helper hides itself under a box, RW 0x97E137 -> RW 0x953498(0))
	}
	if (shown == m_waitShown)
	{
		return;
	}
	if (!m_noticeBox)
	{
		m_noticeBox = std::make_unique<AptMessageBox>(windows(), shell(), "StrategicMessageBox");
	}
	if (m_noticeBox->level() < 0)
	{
		windows().note("strategic-notice", "no StrategicMessageBox (GuiFX.apt is not loaded): the wait message cannot be shown");
		return;
	}
	m_waitShown = shown;
	if (shown)
	{
		m_noticeBox->show(AptMessageBox::TYPE_NON_INTERACTIVE, std::u16string(), fetchOrMissing(m_env.gameText, "STRATEGICHUD:WaitMessage"));
	}
	else
	{
		m_noticeBox->hide();
	}
}

bool AptStrategicHUD::openUnitSwapper(const SwapperSlot &top, const SwapperSlot &bottom)
{
	auto frame = m_parts.find("LoadDialogFrame");
	if (frame == m_parts.end() || !m_helpBox.isLoaded() || m_promptWanted)
	{
		return false; // RW 0x9F9376: no dialog frame or no help box, nothing opens
	}
	if (m_swapWanted)
	{
		releaseSwapper(true);
	}
	m_swapWanted = true;
	m_swapState = 0;
	m_swapSlots[0] = top;
	m_swapSlots[1] = bottom;
	for (bool &b : m_swapButtons)
	{
		b = false;
	}
	m_swapTexts.clear();
	std::string error;
	if (!windows().invokeASAt(level(), relativePath(frame->second), "LoadContent", { "StrategicArmyUnitSwapper.swf" }, nullptr, &error)) // RW 0xC92030
	{
		windows().note("invoke-failed", "StrategicHUD dialog frame LoadContent(StrategicArmyUnitSwapper.swf): " + error);
		m_swapWanted = false;
		return false;
	}
	m_log.push_back("DialogFrame.LoadContent(StrategicArmyUnitSwapper.swf)");
	return true;
}

void AptStrategicHUD::onSwapperLoaded(const std::string &clip)
{
	// RW 0xA08B10 -> the movie clip RW 0xA119EF: its commands under "_level%u." + path, the six records cleared, then each slot's texts (RW 0xA08736)
	m_swapClip = relativePath(clip);
	const std::string base = "_level" + std::to_string(level()) + "." + m_swapClip;
	auto add = [&](const std::string &suffix, WindowManager::CommandFn fn) {
		if (registerCommand(base + suffix, std::move(fn)))
		{
			m_swapCommands.push_back(base + suffix);
		}
	};
	add("_OnOpen", [this](const std::string &) { // RW 0xA10F9D
		if (m_swapState == 0)
		{
			m_swapState = 1;
		}
	});
	add("_OnClosed", [this](const std::string &) { // RW 0xA10FAD -> the holder's OnClosed RW 0x9F8796
		if (m_swapState == 2)
		{
			m_swapState = 3;
			releaseSwapper(true);
			m_requests.push_back("swapper_closed");
		}
	});
	// the buttons (RW 0xA10FC4 ..): only while open; WindowManager + 0x318 (UNRESOLVED [S-1334]) taken as 0
	add("_OnExitButtonClicked", [this](const std::string &) {
		if (m_swapState != 1)
		{
			return;
		}
		std::string err;
		windows().invokeASAt(level(), m_swapClip, "FadeOut", {}, nullptr, &err); // RW 0xA11318
		m_swapState = 2;
	});
	const char *const buttons[3][2] = { { "_OnMoveDownButtonClicked", "swapper_move_down" }, { "_OnMoveUpButtonClicked", "swapper_move_up" },
		{ "_OnSwapButtonClicked", "swapper_swap" } };
	for (const auto &b : buttons)
	{
		const std::string request = b[1];
		add(b[0], [this, request](const std::string &) {
			if (m_swapState == 1)
			{
				m_requests.push_back(request);
			}
		});
	}
	for (int slot = 0; slot < 2; ++slot)
	{
		add(slot == 0 ? "_OnTopPanelLoaded" : "_OnBottomPanelLoaded", [this, slot](const std::string &path) { // CreatePanel RW 0xA113C0
			ArmyPanelView &v = m_swapViews[slot];
			if (!v.clip.empty())
			{
				return;
			}
			const SwapperSlot &s = m_swapSlots[slot];
			v.panel = s.panel;
			v.backButton = false;
			v.hovered = -1;
			v.leaderSelected = false; // no SelectAll here (RW 0xA0882C): the leader starts "_up"
			v.selected.assign(s.panel.members.size(), false);
			for (size_t i = 0; i < s.keys.size() && i < v.selected.size(); ++i)
			{
				v.selected[i] = std::find(s.selectedKeys.begin(), s.selectedKeys.end(), s.keys[i]) != s.selectedKeys.end();
			}
			onArmyContentLoaded(v, relativePath(path));
		});
	}
	for (const char *p : { "Top", "Bottom" })
	{
		for (const char *n : { "RegionName", "ArmyName", "CP" })
		{
			windows().setAptText("APT:" + base + "_" + p + n, std::string()); // RW 0xA1134C(slot, name, "")
		}
	}
	writeSwapperSlot(0);
	writeSwapperSlot(1);
}

void AptStrategicHUD::writeSwapperSlot(int slot)
{
	// InitMovieClipSlot RW 0xA08736: the region's DisplayName, GetDisplayName (RW 0x9D8AA5), the CP (RW 0xA110CE: "" without a maximum); each record only
	// when its text changed
	if (m_swapClip.empty())
	{
		return;
	}
	const SwapperSlot &s = m_swapSlots[slot];
	const std::string base = "APT:_level" + std::to_string(level()) + "." + m_swapClip + "_" + (slot == 0 ? "Top" : "Bottom");
	std::string cp;
	if (s.maxCp > 0)
	{
		cp = loadScreenU16ToUtf8(LivingWorldSetupText::format(fetchOrMissing(m_env.gameText, "STRATEGICHUD:ArmyUnitSwapperCP"),
			{ LivingWorldSetupText::FormatArg(s.cp), LivingWorldSetupText::FormatArg(s.maxCp) }));
	}
	const std::pair<std::string, std::string> records[3] = { { base + "RegionName", s.regionName }, { base + "ArmyName", s.armyName }, { base + "CP", cp } };
	for (const auto &r : records)
	{
		auto it = m_swapTexts.find(r.first);
		if (it == m_swapTexts.end() || it->second != r.second)
		{
			windows().setAptText(r.first, r.second);
			m_swapTexts[r.first] = r.second;
			m_statRecords[r.first] = r.second;
		}
	}
}

void AptStrategicHUD::updateUnitSwapper(const SwapperSlot &top, const SwapperSlot &bottom)
{
	if (!m_swapWanted)
	{
		return;
	}
	const SwapperSlot slots[2] = { top, bottom };
	for (int i = 0; i < 2; ++i)
	{
		const bool changed = slots[i].keys != m_swapSlots[i].keys;
		const std::vector<int> keep = swapperSelectedKeys(i);
		m_swapSlots[i] = slots[i];
		m_swapSlots[i].selectedKeys = keep;
		writeSwapperSlot(i); // SetCP after an entry came or went (RW 0xA086A0)
		ArmyPanelView &v = m_swapViews[i];
		if (changed && !v.clip.empty())
		{
			// the panel's own refresh (its army listener): the members again, the selection kept for the keys still there
			// INFERENCE [S-1334]: every icon slot deleted (the movie's DeleteUnitIconSlot) and the members' slots made again
			const std::string clip = v.clip;
			std::string err;
			for (const ArmySlot &slot : v.slots)
			{
				windows().invokeASAtValues(level(), clip, "DeleteUnitIconSlot", { AptValue::integer(slot.id) }, &err);
			}
			releaseArmyView(v);
			v.panel = m_swapSlots[i].panel;
			v.selected.assign(v.panel.members.size(), false);
			for (size_t k = 0; k < m_swapSlots[i].keys.size() && k < v.selected.size(); ++k)
			{
				v.selected[k] = std::find(keep.begin(), keep.end(), m_swapSlots[i].keys[k]) != keep.end();
			}
			onArmyContentLoaded(v, clip);
		}
	}
}

void AptStrategicHUD::setSwapperButtons(bool moveDown, bool moveUp, bool swap)
{
	if (m_swapClip.empty())
	{
		return;
	}
	const bool want[3] = { moveDown, moveUp, swap };
	static const char *const names[3] = { "moveDown", "moveUp", "swap" }; // RW 0xC92D70 / 0xC92D7C / 0xC92D84
	for (int i = 0; i < 3; ++i)
	{
		if (want[i] != m_swapButtons[i])
		{
			std::string err;
			if (!windows().invokeASAt(level(), m_swapClip, "SetButtonState", { names[i], want[i] ? "_up" : "_disabled" }, nullptr, &err))
			{
				windows().note("invoke-failed", std::string("StrategicArmyUnitSwapper SetButtonState ") + names[i] + ": " + err);
			}
			m_swapButtons[i] = want[i];
		}
	}
}

std::vector<int> AptStrategicHUD::swapperSelectedKeys(int slot) const
{
	std::vector<int> out;
	const ArmyPanelView &v = m_swapViews[slot];
	const SwapperSlot &s = m_swapSlots[slot];
	if (v.clip.empty())
	{
		return s.selectedKeys;
	}
	for (size_t i = 0; i < v.selected.size() && i < s.keys.size(); ++i)
	{
		if (v.selected[i])
		{
			out.push_back(s.keys[i]);
		}
	}
	if (v.panel.heroArmy && v.leaderSelected && !s.keys.empty())
	{
		// the leader toggles SelectAll: every member is in the set already
	}
	std::sort(out.begin(), out.end()); // the set's order (RW 0xA08B97)
	return out;
}

void AptStrategicHUD::closeUnitSwapper()
{
	if (m_swapWanted)
	{
		releaseSwapper(true);
	}
}

void AptStrategicHUD::releaseSwapper(bool unload)
{
	// the dialog Impl's dtor RW 0xA090FD: the frame's content unloaded, the movie clip and both panels released
	for (ArmyPanelView &v : m_swapViews)
	{
		releaseArmyView(v);
	}
	for (const std::string &name : m_swapCommands)
	{
		windows().unregisterCommand(name);
	}
	m_swapCommands.clear();
	const bool hadContent = !m_swapClip.empty() || m_swapWanted;
	m_swapClip.clear();
	m_swapWanted = false;
	m_swapState = 0;
	auto it = m_parts.find("LoadDialogFrame");
	if (unload && hadContent && it != m_parts.end())
	{
		std::string error;
		windows().invokeASAt(level(), relativePath(it->second), "UnloadContent", {}, nullptr, &error); // RW 0x98635C
		m_log.push_back("DialogFrame.UnloadContent");
	}
}

void AptStrategicHUD::closeBattlePrompt()
{
	if (!m_promptWanted)
	{
		return;
	}
	m_promptWanted = false;
	if (m_promptHelp && m_helpBox.help() == m_promptHelp.get())
	{
		m_helpBox.hideSoon(); // WOTR2 milestone 8: the Real Time reason goes with the prompt
	}
	m_promptHelp.reset();
	for (const std::string &name : m_promptCommands)
	{
		windows().unregisterCommand(name);
	}
	m_promptCommands.clear();
	m_promptPath.clear();
	auto it = m_parts.find("LoadDialogFrame");
	if (it != m_parts.end())
	{
		std::string error;
		if (!windows().invokeASAt(level(), relativePath(it->second), "UnloadContent", {}, nullptr, &error))
		{
			windows().note("invoke-failed", "StrategicHUD dialog frame UnloadContent: " + error);
		}
		m_log.push_back("DialogFrame.UnloadContent");
	}
}

std::vector<std::string> AptStrategicHUD::takeRequests()
{
	std::vector<std::string> out;
	out.swap(m_requests);
	return out;
}
