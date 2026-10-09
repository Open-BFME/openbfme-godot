// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptTimeLine.h (lane END-1).

#include "GameClient/GUI/AptScreens/AptTimeLine.h"

#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace
{
// the statistics texts are kept as UTF-8 (ScoreScreenData); the list box takes UTF-16
UnicodeString utf8ToU16(const std::string &s)
{
	UnicodeString out;
	for (size_t i = 0; i < s.size();)
	{
		const unsigned char c = (unsigned char)s[i];
		std::uint32_t cp = c;
		size_t n = 1;
		if (c >= 0xF0 && i + 3 < s.size())
		{
			cp = ((c & 0x07u) << 18) | (((unsigned char)s[i + 1] & 0x3Fu) << 12) | (((unsigned char)s[i + 2] & 0x3Fu) << 6) | ((unsigned char)s[i + 3] & 0x3Fu);
			n = 4;
		}
		else if (c >= 0xE0 && i + 2 < s.size())
		{
			cp = ((c & 0x0Fu) << 12) | (((unsigned char)s[i + 1] & 0x3Fu) << 6) | ((unsigned char)s[i + 2] & 0x3Fu);
			n = 3;
		}
		else if (c >= 0xC0 && i + 1 < s.size())
		{
			cp = ((c & 0x1Fu) << 6) | ((unsigned char)s[i + 1] & 0x3Fu);
			n = 2;
		}
		if (cp >= 0x10000u)
		{
			cp -= 0x10000u;
			out.push_back((char16_t)(0xD800u + (cp >> 10)));
			out.push_back((char16_t)(0xDC00u + (cp & 0x3FFu)));
		}
		else
		{
			out.push_back((char16_t)cp);
		}
		i += n;
	}
	return out;
}

const char *const kProviders[6] = { "TimeLine:ScreenMode", "TimeLine:ShowSaveReplay", "TimeLine:NumOfPlayers", "TimeLine:NumCahAwards",
	"TimeLine:LocalPlayerIsObserver", "TimeLine:StrategicEnd" }; // RW 0xC7E0E8
} // namespace

AptTimeLine::AptTimeLine(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "TimeLine.apt", "AptTimeLine"), m_env(environment)
{
	registerCommand("AptTimeLine::OnInitialized", [this](const std::string &) { populate(); });
	registerCommand("AptTimeLine::OnButtonContinue", [this](const std::string &argument) {
		++m_continue;
		if (m_env.services)
		{
			m_env.services->request(ShellRequest{ ShellAction::ScoreScreenContinue, argument });
		}
		else
		{
			this->windows().note("command-unwired", "AptTimeLine::OnButtonContinue: the host gave no shell services [S-1063]");
		}
	});
	registerCommand("AptTimeLine::OnButtonSaveReplay", [this](const std::string &argument) {
		if (m_env.services)
		{
			m_env.services->request(ShellRequest{ ShellAction::ScoreScreenSaveReplay, argument });
		}
		this->windows().note("unported-command", "AptTimeLine::OnButtonSaveReplay: saving the replay is not ported [S-1063]");
	});
	registerCommand("AptTimeLine::CaHAwardNumber", unportedCommand("AptTimeLine::CaHAwardNumber"));
	// RW 0x9CD723 (registered by the statistics object, RW 0x9CE5CA)
	registerCommand("AptTimeLine::SetPlayerFocus", [this](const std::string &argument) {
		const ScoreScreenData *d = data();
		if (!d)
		{
			return;
		}
		m_shown = ScoreScreenData::focusColumns(std::atoi(argument.c_str()), (int)d->entries.size(), d->localIsObserver, m_shown);
		if (statsList())
		{
			fillStats();
		}
	});
	// RW 0x9CE51E
	registerScreenRef("AptTimeLine::InitGadgets", [this](const std::string &name, GameWindow *w) {
		const ScoreScreenData *d = data();
		if (name != "AptTimeLine::StatsList" || !w || !d)
		{
			return;
		}
		m_statsList = w;
		if (d->entries.size() == 1)
		{
			const int widths[2] = { 50, 50 }; // RW 0x9CE4DF(2, 0x32)
			GadgetListBoxSetColumnWidths(w, 2, widths);
		}
		else
		{
			const int widths[4] = { 25, 25, 25, 25 }; // RW 0x9CE4DF(4, 0x19)
			GadgetListBoxSetColumnWidths(w, 4, widths);
		}
		fillStats();
	});
	// RW 0x925026: every provider writes "0" before it answers
	for (int i = 0; i < 6; ++i)
	{
		registerProvider(kProviders[i], [this, i](const std::string &name, std::string &value, bool setting) {
			if (setting)
			{
				return true;
			}
			value = "0";
			const ScoreScreenData *d = data();
			if (!d)
			{
				this->windows().note("provider-unwired", name + ": the host gave no ScoreScreenData [S-1063]");
				return true;
			}
			switch (i)
			{
				case 0:
					if (d->type == 1 || d->type == 2 || d->type == 8)
					{
						value = "OtherSingle";
					}
					else if (d->type >= 3 && d->type <= 5)
					{
						value = "OtherOnlineLan";
					}
					else if (d->type == 6)
					{
						value = "WOTRSingle";
					}
					else if (d->type == 7)
					{
						value = "WOTROnlineLan";
					}
					break;
				case 1:
					// GlobalData + 0x9D4 & 3 (not identified: taken as clear, S-1063)
					value = (d->type == 3 || d->type == 4) ? "1" : "0";
					break;
				case 2:
					value = std::to_string(d->entries.size());
					break;
				case 3:
					value = "0"; // no Create-a-Hero awards outside the campaign / War of the Ring
					break;
				case 4:
					value = d->localIsObserver ? "1" : "0";
					break;
				case 5:
					value = "0"; // only the War of the Ring types 6 / 7 answer more
					break;
			}
			return true;
		});
	}
	// the flag and count answers reach the movie as numbers (WindowManager::markNumericProvider, S-1063)
	for (const char *name : { "TimeLine:ShowSaveReplay", "TimeLine:NumOfPlayers", "TimeLine:NumCahAwards", "TimeLine:LocalPlayerIsObserver", "TimeLine:StrategicEnd" })
	{
		windows.markNumericProvider(name);
	}
	for (int i = 0; i < 8; ++i)
	{
		// RW 0x925188
		registerProvider("TimeLine:PlayerColor:" + std::to_string(i), [this, i](const std::string &, std::string &value, bool setting) {
			if (setting)
			{
				return true;
			}
			value = "0";
			const ScoreScreenData *d = data();
			if (d && i < (int)d->entries.size())
			{
				char b[32];
				std::snprintf(b, sizeof(b), "%#x", d->entries[(size_t)i].color & 0xFFFFFFu);
				value = b;
			}
			return true;
		});
		// RW 0x925569
		registerProvider("TimeLine:PlayerFaction:" + std::to_string(i), [this, i](const std::string &, std::string &value, bool setting) {
			if (setting)
			{
				return true;
			}
			value.clear();
			const ScoreScreenData *d = data();
			if (d && i < (int)d->entries.size())
			{
				value = d->entries[(size_t)i].side;
			}
			return true;
		});
		// RW 0x92536C: setting only
		registerProvider("TimeLine:GraphFocus:" + std::to_string(i), [this, i](const std::string &, std::string &value, bool setting) {
			if (setting)
			{
				m_focus[(size_t)i] = (float)std::atoi(value.c_str()) * 0.01f;
			}
			return true;
		});
	}
	registerComponent("AptTimeLine::RenderGraph", [this](AptComponentRequest &) -> std::shared_ptr<GameWindow> {
		++m_componentRequests; // the device layer draws the graph from graph() (S-1063)
		return nullptr;
	});
}

const ScoreScreenData *AptTimeLine::data() const
{
	return m_env.scoreScreen && m_env.scoreScreen->valid ? m_env.scoreScreen : nullptr;
}

void AptTimeLine::runInit()
{
	AptScreen::runInit();
	populate();
}

// RW 0x92632E: the names (and the faction icon images, not ported)
void AptTimeLine::populate()
{
	const ScoreScreenData *d = data();
	if (!d)
	{
		windows().note("timeline-unwired", "no ScoreScreenData: the score screen shows the movie's own texts [S-1063]");
		return;
	}
	for (size_t i = 0; i < d->entries.size(); ++i)
	{
		windows().setAptText("TimeLine:PlayerName:" + std::to_string(i), d->entries[i].name);
		windows().setAptImage("TimeLine:PlayerFactionIcon:" + std::to_string(i), "AptIcon" + d->entries[i].side); // RW 0x92632E (lane END-2)
	}
	graph(4);
	// lane END-2: the statistics rows (RW 0x9CDEC1 per entry), formatted with the game text
	m_rows = d->statRows([this](const std::string &label) { return loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, label)); });
}

GameWindow *AptTimeLine::statsList()
{
	// the gadget goes with its page: a window the layer no longer holds is forgotten
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (m_statsList && layer)
	{
		const std::vector<GameWindow *> live = layer->gadgets().allWindows();
		if (std::find(live.begin(), live.end(), m_statsList) == live.end())
		{
			m_statsList = nullptr;
		}
	}
	return m_statsList;
}

// RW 0x9F13D8 / 0x9F0B50
void AptTimeLine::fillStats()
{
	GameWindow *list = statsList();
	if (!list)
	{
		return;
	}
	++m_fills;
	const int top = GadgetListBoxGetTopVisibleEntry(list); // RW 0x7262AC
	GadgetListBoxReset(list);                              // RW 0x726231
	const Color white = 0xFFFFFFFFu;                       // RW 0xDEC194
	for (const ScoreScreenData::StatRow &row : m_rows)
	{
		if (!row.shown)
		{
			continue; // + 0x14
		}
		const int r = GadgetListBoxAddEntryText(list, utf8ToU16(row.label), white, -1, 0);
		int column = 1;
		for (int idx : m_shown)
		{
			if (idx >= 0 && (size_t)idx < row.values.size()) // an unsigned compare with the row's 8 values (-1 is skipped)
			{
				const ScoreScreenData::StatCell &c = row.values[(size_t)idx];
				const Color color = c.value == row.best ? white : (Color)0xFF7FAABBu; // RW 0x9F082F
				GadgetListBoxAddEntryText(list, utf8ToU16(c.text), color, r, column);
				GadgetListBoxSetCellJustification(list, r, column, 2); // RW 0x9F0876
			}
			++column;
		}
		GadgetListBoxAddEntryText(list, utf8ToU16(" "), white, -1, 0); // RW 0xBD16E4
	}
	GadgetListBoxSetTopVisibleEntry(list, top); // RW 0x726E98
}

const ScoreScreenData::Axis &AptTimeLine::graph(int mode)
{
	const ScoreScreenData *d = data();
	if (!d)
	{
		return m_axis;
	}
	std::string formats[3];
	for (int i = 0; i < 3; ++i)
	{
		formats[i] = loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, ScoreScreenData::kTimeFormatLabels[i]));
	}
	m_axis = d->axis(mode, formats);
	m_mode = mode;
	if (m_axis.top != m_lastTop) // RW 0x925CA4: + 0x2B0
	{
		for (int i = 0; i <= 10; ++i)
		{
			windows().setAptText("Timeline:YAxis:" + std::to_string(i), m_axis.yLabels[(size_t)i]);
		}
		m_lastTop = m_axis.top;
	}
	if (m_axis.samples != m_lastSamples) // + 0x2B4
	{
		windows().setAptText("Timeline:XAxisDescription", loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, ScoreScreenData::kTimeDescriptionLabels[m_axis.timeFormat])));
		for (int i = 0; i <= 10; ++i)
		{
			windows().setAptText("Timeline:XAxis:" + std::to_string(i), m_axis.xLabels[(size_t)i]);
		}
		windows().setAptText("Timeline:TotalTime", m_axis.totalTime);
		m_lastSamples = m_axis.samples;
	}
	return m_axis;
}
