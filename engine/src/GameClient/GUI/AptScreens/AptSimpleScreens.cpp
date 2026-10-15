// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptSimpleScreens.h.

#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/PlayerStatusInfo.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GameLODManager.h"
#include "GameClient/GUI/TributeInfo.h"
#include "GameClient/OptionPreferences.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"

#include "GameClient/GUI/GameTextSource.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <array>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

AptLevel0Screen::AptLevel0Screen(WindowManager &windows, Shell &shell) : AptScreen(windows, shell, "AptLevel0.apt", "", -1, true) {}

void AptLevel0Screen::runShutdown(bool *)
{
	// the base movie stays up under every screen: shutting this screen down does not hide it
	shell().shutdownComplete(this);
}

AptBackgroundScreen::AptBackgroundScreen(WindowManager &windows, Shell &shell) : AptScreen(windows, shell, "Background.apt", "") {}

AptGuiFXScreen::AptGuiFXScreen(WindowManager &windows, Shell &shell, bool adoptLoaded)
	: AptScreen(windows, shell, "GuiFX.apt", "AptGuiFX", 11, adoptLoaded)
{
	if (adoptLoaded)
	{
		return; // the window's owner registered the movie's commands
	}
	registerCommand("AptGuiFX::OnInitialized", unportedCommand("AptGuiFX::OnInitialized"));
	// lane HOVER-1: the tool tip's text has no window: the render callback "ToolTipText" (CursorTooltip.h, RW 0x782D52) draws it in the clip's place
	registerComponent("ToolTipText", [](AptComponentRequest &) -> std::shared_ptr<GameWindow> { return nullptr; });
}

// lane PLAY-1: PlayerTribute.apt (see the header)
// lane PLAY-1: PlayerTribute.apt (see the header)
AptPlayerTribute::AptPlayerTribute(WindowManager &windows, Shell &shell, ShellServices &services, ShellEnvironment &environment)
	: AptScreen(windows, shell, "PlayerTribute.apt", "AptPlayerTribute"), m_services(services), m_env(environment)
{
	if (level() < 0)
	{
		return;
	}
	if (!m_env.tribute)
	{
		windows.note("tribute-unwired", "PlayerTribute.apt: no game to send tribute in (ShellEnvironment::tribute is null) [S-1922]");
	}
	const std::string p = pathPrefix();
	registerCommand(p + "_ReturnToGame", [this](const std::string &argument) {
		m_services.request(ShellRequest{ ShellAction::TributeReturnToGame, argument }); // RW 0x914E89 -> RW 0x914C51
	});
	// lane HUD-5: the pages (RW 0x91741F / 0x9171E3 / 0x917258); OnInitialized's body is empty in retail (BFME2 decomp: the folded empty method 0x0047A69C)
	registerCommand(p + "_OnInitialized", [](const std::string &) {});
	registerCommand(p + "_OnPageLoaded", [this](const std::string &argument) { pageLoaded(argument); });
	registerCommand(p + "_OnPageUnloaded", [this](const std::string &argument) { pageUnloaded(argument); });
	registerCommand(p + "_OnPageSelected", [](const std::string &) {
		// RW 0x917258: the current page changes; the Status page's show / hide (its base's vslots 1 / 2) change no text
	});
	registerCommand("AptPlayerTribute::OnInitialized", [](const std::string &) {});
	windows.addUpdateListener(this, [this]() { update(); }); // lane PLAY-1 r2: the TributePage's update (RW 0x9164EB)
	// RW 0x914C08: "1" when the local player is active (lane PLAY-1 r2)
	registerProvider(p + "_TributeEnabled", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			return true;
		}
		value = m_env.tribute && m_env.tribute->localActive() ? "1" : "0";
		return true;
	});
}

AptPlayerTribute::~AptPlayerTribute()
{
	dropStatusPage();
}

// RW 0x81560E
bool AptPlayerTribute::commandParam(const std::string &params, const std::string &key, std::string &value)
{
	if (params.empty() || key.empty())
	{
		return false;
	}
	const char *s = params.c_str();
	auto skipSpace = [](const char *&q) {
		while (*q && std::isspace((unsigned char)*q))
		{
			++q;
		}
	};
	const char *p = s;
	for (;;)
	{
		skipSpace(p);
		if (!*p)
		{
			return false;
		}
		bool match = false;
		if (std::strncmp(p, key.c_str(), key.size()) == 0)
		{
			const char *q = p + key.size();
			skipSpace(q);
			match = *q == '=';
		}
		while (*p) // past the first '='
		{
			const char c = *p++;
			if (c == '=')
			{
				break;
			}
		}
		skipSpace(p);
		if (match)
		{
			const char *e = p;
			while (*e && *e != '&')
			{
				++e;
			}
			value.assign(p, e);
			return true;
		}
		while (*p) // past the next '&'
		{
			const char c = *p++;
			if (c == '&')
			{
				break;
			}
		}
	}
}

// RW 0x8155D9: sscanf(path, "_level%d")
int AptPlayerTribute::levelIndexFromTarget(const std::string &path)
{
	int n = 0;
	return std::sscanf(path.c_str(), "_level%d", &n) == 1 ? n : -1;
}

// RW 0x815563
std::string AptPlayerTribute::skipLevelN(const std::string &path)
{
	if (path.compare(0, 6, "_level") != 0)
	{
		return path;
	}
	size_t i = 6;
	while (i < path.size() && path[i] != '/' && path[i] != '.')
	{
		++i;
	}
	if (i < path.size())
	{
		++i;
	}
	return path.substr(i);
}

int AptPlayerTribute::statusRowsShown() const
{
	int n = 0;
	if (m_status)
	{
		for (bool b : m_status->rowShown)
		{
			n += b ? 1 : 0;
		}
	}
	return n;
}

// RW 0x91741F (OnPageLoaded) -> the page factory (StatusPage: the row list RW 0x9166AC)
void AptPlayerTribute::pageLoaded(const std::string &params)
{
	std::string name, type;
	if (!commandParam(params, "name", name) || levelIndexFromTarget(name) != level() || !commandParam(params, "type", type))
	{
		return;
	}
	if (type == "TributePage")
	{
		// lane PLAY-1 r2: RW 0x916E8F
		m_tributePage = skipLevelN(name);
		const std::string page = pathPrefix() + "." + m_tributePage;
		registerCommand(page + "_OnEnabledContentLoaded", [this](const std::string &arg) { contentLoaded(arg); });
		// RW 0x91653C -> RW 0x915946: the content and its rows are freed
		registerCommand(page + "_OnEnabledContentUnloaded", [this](const std::string &) {
			m_content.clear();
			m_rows.clear();
		});
		return;
	}
	if (type != "StatusPage")
	{
		return; // RW 0x91741F: no page for another type
	}
	dropStatusPage(); // the page map's operator[] replaces a page under the same name
	if (!m_env.playerStatus)
	{
		windows().note("provider-unwired", "StatusPage: the host gave no PlayerStatusInfo [S-1953]");
	}
	m_status = std::make_unique<StatusPage>();
	m_statusName = name;
	m_status->path = skipLevelN(name);
	const size_t rows = m_env.playerStatus ? m_env.playerStatus->rows.size() : 0;
	m_status->rowShown.assign(rows, false);
	m_status->rowPath.assign(rows, std::string());
	const std::string prefix = statusPrefix();
	registerCommand(prefix + "_OnRowShown", [this](const std::string &argument) { rowShown(argument); });
	registerCommand(prefix + "_OnRowHidden", [this](const std::string &argument) { rowHidden(argument); });
	// RW 0x914AA1: "0" by default, the row count / InSkirmish otherwise
	registerProvider(prefix + "_NumOfPlayers", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = std::to_string(m_status ? m_status->rowShown.size() : 0);
		}
		return true;
	});
	registerProvider(prefix + "_InSkirmish", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = m_env.playerStatus && m_env.playerStatus->inSkirmish ? "1" : "0";
		}
		return true;
	});
}

// RW 0x9171E3: the argument is the page's name
void AptPlayerTribute::pageUnloaded(const std::string &name)
{
	if (m_status && name == m_statusName)
	{
		dropStatusPage();
	}
}

void AptPlayerTribute::dropStatusPage()
{
	if (!m_status)
	{
		return;
	}
	const std::string prefix = statusPrefix();
	for (size_t i = 0; i < m_status->rowShown.size(); ++i)
	{
		if (m_status->rowShown[i])
		{
			windows().unregisterProvider("_level" + std::to_string(level()) + "." + m_status->rowPath[i] + "_color");
		}
	}
	for (const char *n : { "_OnRowShown", "_OnRowHidden" })
	{
		windows().unregisterCommand(prefix + n);
	}
	for (const char *n : { "_NumOfPlayers", "_InSkirmish" })
	{
		windows().unregisterProvider(prefix + n);
	}
	m_status.reset();
	m_statusName.clear();
}

// RW 0x915E00 (OnRowShown) -> the row object RW 0x915BF6
void AptPlayerTribute::rowShown(const std::string &params)
{
	std::string indexText, name;
	if (!m_status || !commandParam(params, "index", indexText))
	{
		return;
	}
	const int index = std::atoi(indexText.c_str());
	const int count = (int)m_status->rowShown.size();
	if (index < 0 || index > count)
	{
		return;
	}
	if (index == count)
	{
		// retail builds a row for the empty slot past the rows (index > count is its only test); its slot -1 has no GameSlot (INFERENCE: not reached by the movie)
		windows().note("row-past-end", "StatusPage row " + indexText + " of " + std::to_string(count) + " [S-1953]");
		return;
	}
	if (m_status->rowShown[(size_t)index] || !commandParam(params, "name", name) || levelIndexFromTarget(name) != level())
	{
		return;
	}
	const PlayerStatusRow &row = m_env.playerStatus->rows[(size_t)index];
	const std::string path = skipLevelN(name);
	m_status->rowShown[(size_t)index] = true;
	m_status->rowPath[(size_t)index] = path;
	for (int f = 0; f < 4; ++f) // RW 0x915159: bfmeSetText("APT:_level%u.%s_field%d")
	{
		windows().setAptText("APT:_level" + std::to_string(level()) + "." + path + "_field" + std::to_string(f), row.fields[f]);
	}
	const std::int32_t color = row.color;
	registerProvider("_level" + std::to_string(level()) + "." + path + "_color", [color](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = std::to_string(color); // RW 0x914A69: sprintf("%d", + 0x58)
		}
		return true;
	});
}

// RW 0x915269 (OnRowHidden): the row object goes (its texts stay as written)
void AptPlayerTribute::rowHidden(const std::string &params)
{
	std::string indexText;
	if (!m_status || !commandParam(params, "index", indexText))
	{
		return;
	}
	const int index = std::atoi(indexText.c_str());
	if (index < 0 || index >= (int)m_status->rowShown.size() || !m_status->rowShown[(size_t)index])
	{
		return;
	}
	windows().unregisterProvider("_level" + std::to_string(level()) + "." + m_status->rowPath[(size_t)index] + "_color");
	m_status->rowShown[(size_t)index] = false;
}

// ---- lane PLAY-1 r2: the tribute page ----

void AptPlayerTribute::setFieldUtf8(const std::string &row, int field, const std::string &text)
{
	// RW 0x915159: "APT:_level%u.%s_field%d"
	windows().setAptText("APT:" + pathPrefix() + "." + row + "_field" + std::to_string(field), text);
}

void AptPlayerTribute::registerRowColor(const std::string &row, std::uint32_t color)
{
	registerProvider(pathPrefix() + "." + row + "_color", [color](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = std::to_string((std::int32_t)color); // RW 0x914A69: sprintf "%d" of row + 0x58
		}
		return true;
	});
}

// RW 0x916BF4 -> RW 0x916973
void AptPlayerTribute::contentLoaded(const std::string &argument)
{
	std::string name;
	if (!commandParam(argument, "name", name) && argument.find('=') == std::string::npos)
	{
		name = argument;
	}
	if (name.empty() || !m_content.empty())
	{
		return;
	}
	m_content = pathPrefix() + "." + skipLevelN(name);
	const std::string c = m_content;
	registerCommand(c + "_OnRowShown", [this](const std::string &arg) { tributeRowShown(arg); });
	registerCommand(c + "_OnRowHidden", [this](const std::string &arg) { tributeRowHidden(arg); });
	registerCommand(c + "_Reset", [this](const std::string &) { reset(); });
	registerCommand(c + "_Send", [this](const std::string &) { send(); });
	registerProvider(c + "_NumOfPlayers", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = std::to_string(m_rows.size());
		}
		return true;
	});
	// RW 0x914B54: the local player, then the active allies (at most 7)
	m_rows.clear();
	if (m_env.tribute)
	{
		for (const TributePlayerRow &p : m_env.tribute->tributePlayers())
		{
			if (m_rows.size() >= 7)
			{
				break;
			}
			TRow r;
			r.playerIndex = p.playerIndex;
			r.name = p.name;
			r.cash = p.cash;
			r.local = p.local;
			r.active = p.active;
			r.color = p.color;
			m_rows.push_back(r);
		}
	}
	rebalance();
}

// RW 0x9162F5 -> RW 0x915F5D
// RW 0x9153F2: the row of "index" lets its clip and gadgets go
void AptPlayerTribute::tributeRowHidden(const std::string &argument)
{
	std::string idx;
	if (!commandParam(argument, "index", idx))
	{
		return;
	}
	const int index = std::atoi(idx.c_str());
	if (index < 0 || (size_t)index >= m_rows.size())
	{
		return;
	}
	TRow &row = m_rows[(size_t)index];
	row.path.clear();
	row.slider = nullptr;
	row.entry = nullptr;
	row.shownValue = -1;
}

void AptPlayerTribute::tributeRowShown(const std::string &argument)
{
	std::string idx, name;
	if (!commandParam(argument, "index", idx) || !commandParam(argument, "name", name))
	{
		return;
	}
	const int index = std::atoi(idx.c_str());
	if (index < 0 || (size_t)index >= m_rows.size() || !m_rows[(size_t)index].path.empty())
	{
		return;
	}
	TRow &row = m_rows[(size_t)index];
	row.path = skipLevelN(name);
	const std::string root = pathPrefix() + "." + row.path;
	registerScreenRef(root + "_InitSlider", [this, index](const std::string &, GameWindow *window) {
		// RW 0x914B07: range 0 .. 99999, position = the amount
		TRow &r = m_rows[(size_t)index];
		r.slider = window;
		window->manager().winSendSystemMsg(window, GSM_SET_MIN_MAX, 0, 99999);
		GadgetSliderSetPosition(window, (int)r.amount);
	});
	registerScreenRef(root + "_InitTextEntry", [this, index](const std::string &, GameWindow *window) {
		m_rows[(size_t)index].entry = window; // RW 0x91565C (its 5-digit limit and filter: S-1922)
		refreshRow(m_rows[(size_t)index]);
	});
	setFieldUtf8(row.path, 0, row.name);
	registerRowColor(row.path, row.color);
	refreshRow(row);
}

// RW 0x9155C2 (field 1) and RW 0x915392 (the text entry)
void AptPlayerTribute::refreshRow(TRow &row)
{
	dropDeadGadgets(row);
	if (row.path.empty())
	{
		return;
	}
	long long amount = (long long)row.amount;
	if (row.local)
	{
		amount = 0; // RW 0x914D8D: minus the amounts of the other rows
		for (const TRow &o : m_rows)
		{
			if (!o.local)
			{
				amount -= (long long)o.amount;
			}
		}
	}
	const long long value = amount + (long long)row.cash;
	if (value != row.shownValue)
	{
		setFieldUtf8(row.path, 1, std::to_string(value));
		row.shownValue = value;
	}
	if (row.entry)
	{
		GadgetTextEntrySetText(row.entry, asciiToU16(std::to_string(row.amount)));
	}
}

// RW 0x91578C: what the local cash leaves for each ally row
void AptPlayerTribute::rebalance()
{
	std::uint32_t cash = 0;
	for (const TRow &r : m_rows)
	{
		if (r.local)
		{
			cash = r.cash;
		}
	}
	std::uint32_t left = cash;
	for (const TRow &r : m_rows)
	{
		if (!r.local && r.active)
		{
			left -= std::min(r.amount, left);
		}
	}
	std::uint32_t remaining = cash;
	for (TRow &r : m_rows)
	{
		if (r.local)
		{
			continue;
		}
		const std::uint32_t max = r.active ? std::min(r.amount + left, remaining) : 0;
		// RW 0x91556B: at most 99999; a lower maximum lowers the amount (and the slider)
		const std::uint32_t capped = std::min<std::uint32_t>(max, 99999);
		if (capped != r.maximum)
		{
			r.maximum = capped;
			if (r.maximum < r.amount)
			{
				setAmount(r, r.maximum, true);
			}
		}
		remaining -= std::min(r.amount, remaining);
	}
	for (TRow &r : m_rows)
	{
		refreshRow(r);
	}
}

// the row's gadgets go with their placeholder clips (a closing movie unloads them before the screen is popped)
void AptPlayerTribute::dropDeadGadgets(TRow &row)
{
	if (row.slider && !windows().isComponentWindow(row.slider))
	{
		row.slider = nullptr;
	}
	if (row.entry && !windows().isComponentWindow(row.entry))
	{
		row.entry = nullptr;
	}
}

void AptPlayerTribute::setAmount(TRow &row, std::uint32_t amount, bool moveSlider)
{
	dropDeadGadgets(row);
	row.amount = amount;
	if (moveSlider && row.slider)
	{
		GadgetSliderSetPosition(row.slider, (int)amount);
	}
	refreshRow(row);
}

bool AptPlayerTribute::setRowAmountBySlider(int index, int position)
{
	if (index < 0 || (size_t)index >= m_rows.size() || !m_rows[(size_t)index].slider)
	{
		return false;
	}
	GameWindow *slider = m_rows[(size_t)index].slider;
	return gadgetMessage(nullptr, GSM_SLIDER_TRACK, (WindowMsgData)(std::uintptr_t)slider, (WindowMsgData)position) == MSG_HANDLED;
}

// RW 0x9161DD: the slider's track (RW 0x9156DC) and the text entry's edits (RW 0x91570C) of a row
WindowMsgHandledType AptPlayerTribute::gadgetMessage(GameWindow *owner, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	// the gadget that reports is the message's first datum (the owner window is the screen's own)
	GameWindow *from = reinterpret_cast<GameWindow *>(data1);
	for (TRow &r : m_rows)
	{
		if (msg == GSM_SLIDER_TRACK && from && from == r.slider)
		{
			std::uint32_t pos = (std::uint32_t)std::max<long long>(0, (long long)(std::intptr_t)data2);
			if (pos != r.amount)
			{
				if (pos > r.maximum)
				{
					pos = r.maximum;
					GadgetSliderSetPosition(r.slider, (int)pos);
				}
				setAmount(r, pos, false);
				rebalance();
			}
			return MSG_HANDLED;
		}
		if ((msg == GEM_UPDATE_TEXT || msg == GEM_EDIT_DONE) && from && from == r.entry)
		{
			const std::u16string text = GadgetTextEntryGetText(r.entry);
			long long v = 0;
			for (char16_t ch : text)
			{
				if (ch < u'0' || ch > u'9')
				{
					break;
				}
				v = v * 10 + (ch - u'0');
				if (v > 0x7FFFFFFF)
				{
					break;
				}
			}
			setAmount(r, (std::uint32_t)std::min<long long>(v, r.maximum), true);
			rebalance();
			return MSG_HANDLED;
		}
	}
	return AptScreen::gadgetMessage(owner, msg, data1, data2);
}

// RW 0x9164EB
void AptPlayerTribute::update()
{
	if (!m_env.tribute)
	{
		return;
	}
	if (!m_enabledSet && !m_tributePage.empty() && m_env.tribute->transferAllowed())
	{
		std::string error;
		if (!windows().invokeASAt(level(), m_tributePage, "SetState", { "_enabled" }, nullptr, &error))
		{
			windows().note("tribute-setstate-failed", m_tributePage + ".SetState(_enabled): " + error);
		}
		m_enabledSet = true;
	}
	if (m_content.empty())
	{
		return;
	}
	// the rows read their players' cash live (RW 0x9155C2: Player + 0x94)
	const std::vector<TributePlayerRow> players = m_env.tribute->tributePlayers();
	for (TRow &r : m_rows)
	{
		for (const TributePlayerRow &p : players)
		{
			if (p.playerIndex == r.playerIndex)
			{
				r.cash = p.cash;
				r.active = p.active;
			}
		}
	}
	rebalance(); // RW 0x91578C, then each row's refresh (RW 0x916225)
}

// RW 0x915862 -> RW 0x91554C
void AptPlayerTribute::reset()
{
	for (TRow &r : m_rows)
	{
		setAmount(r, 0, true);
	}
	rebalance();
}

// RW 0x914DD6
void AptPlayerTribute::send()
{
	if (!m_env.tribute)
	{
		windows().note("tribute-unwired", "_Send: no game [S-1922]");
		return;
	}
	if (!m_env.tribute->localActive())
	{
		return;
	}
	int local = -1;
	for (const TRow &r : m_rows)
	{
		if (r.local)
		{
			local = r.playerIndex;
		}
	}
	for (const TRow &r : m_rows)
	{
		if (!r.local && r.active && r.amount > 0)
		{
			m_env.tribute->send(local, r.playerIndex, r.amount);
			++m_sends;
		}
	}
}

std::vector<AptPlayerTribute::RowState> AptPlayerTribute::tributeRows() const
{
	std::vector<RowState> out;
	for (const TRow &r : m_rows)
	{
		RowState s;
		s.playerIndex = r.playerIndex;
		s.local = r.local;
		s.amount = r.amount;
		s.maximum = r.maximum;
		s.slider = r.slider != nullptr;
		s.entry = r.entry != nullptr;
		out.push_back(s);
	}
	return out;
}

const char *AptPlayerTribute::stopLine()
{
	return "[S-1922] PlayerTribute.apt (the Palantir's flag in a skirmish / multiplayer game, RW 0x6D40C9 -> RW 0x914EF0): the screen opens over the game and closes "
		   "(its Cancel / Escape: <path>_ReturnToGame, RW 0x914C51); the status rows are HUD-5's (S-1953); the tribute rows with their sliders and amounts "
		   "(RW 0x915F5D .. 0x91578C) and Send (RW 0x914DD6 -> MSG_GIVE_MONEY, RW 0x6264E1) are ported (PLAY-1 r2); not ported: the opening's pause / input calls "
		   "(RW 0x914FA2 .. 0x914FE2), a skirmish AI ally's extra money term in a tribute row's field 1 (RW 0x6A950B / 0x6A9999), the text entry's limit and filter "
		   "(RW 0x81606D / 0x721FA9), RW 0x91578C's call points (here after every change), RW 0x6264E1's + 0x754 test taken as Player::isDefeated, and the campaign's "
		   "objectives screen (RW 0x8E8843, AptObjectivesMenu)";
}

AptLoadScreen::AptLoadScreen(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "LoadScreen.apt", "AptGameLoading"), m_env(environment)
{
	// GameLoadingType (RW 0x81C754): "_lan" for mode 1, "_skirmish" for 2, "_internetAdv" for 5, nothing else
	registerProvider("GameLoadingType", [this](const std::string &name, std::string &value, bool setting) {
		if (setting)
		{
			return true;
		}
		if (!m_env.loadScreen)
		{
			this->windows().note("provider-unwired", name + ": the host gave no LoadScreenInfo [S-273]");
			return false;
		}
		switch (m_env.loadScreen->gameLoadingType)
		{
			case 1: value = "_lan"; return true;
			case 2: value = "_skirmish"; return true;
			case 5: value = "_internetAdv"; return true;
			default: return false;
		}
	});
	for (int i = 0; i < MAX_LOAD_SLOTS; ++i)
	{
		// GameLoading:PlayerColor:<c> (RW 0x81C6D2): sprintf("%d", the colour as a signed 32 bit 0xAARRGGBB) for an occupied card, nothing otherwise
		registerProvider("GameLoading:PlayerColor:" + std::to_string(i), [this, i](const std::string &name, std::string &value, bool setting) {
			if (setting)
			{
				return true;
			}
			if (!m_env.loadScreen)
			{
				this->windows().note("provider-unwired", name + ": the host gave no LoadScreenInfo [S-273]");
				return false;
			}
			const LoadScreenSlotInfo &c = m_env.loadScreen->cards[i];
			if (!c.occupied)
			{
				return false;
			}
			value = std::to_string((std::int32_t)(0xFF000000u | (c.color & 0xFFFFFFu)));
			return true;
		});
	}
	populate();
	windows.addUpdateListener(this, [this]() { flush(); });
}

void AptLoadScreen::populate()
{
	// LoadScreen init (RW 0x81CB08): per card the three texts; Rank is a blank; unused cards are emptied
	if (!m_env.loadScreen)
	{
		windows().note("loadscreen-unwired", "no LoadScreenInfo: the cards stay as the movie draws them [S-273]");
		return;
	}
	for (int c = 0; c < MAX_LOAD_SLOTS; ++c)
	{
		const std::string n = std::to_string(c);
		const LoadScreenSlotInfo &card = m_env.loadScreen->cards[c];
		windows().setAptText("LoadingScreen::Rank" + n, " ");
		// lane MENU-1: the Level column's rank icon and an empty Fellowship icon (RW 0x81CFFA .. 0x81D046: the images of the records
		// UIClip/Level/<card> and UIClip/Fellowship/<card>; the fellowship one only in an online fellowship game)
		windows().setAptImage("UIClip/Level/" + n, card.occupied ? card.levelImage : std::string());
		windows().setAptImage("UIClip/Fellowship/" + n, std::string());
		if (card.occupied && card.levelImage.empty())
		{
			windows().note("loadscreen-level", "[S-3506] card " + n + ": no rank icon (no mapped image lookup, or none for its army and level)");
		}
		else if (card.occupied && card.human)
		{
			windows().note("loadscreen-level", "[S-3506] card " + n + ": the player's rank icon " + card.levelImage +
				" is level 1: the profile's <army>Points are not read (the stats files are not ported)");
		}
		if (card.occupied)
		{
			++m_cards;
			windows().setAptText("LoadingScreen::PlayerName" + n, loadScreenU16ToUtf8(card.playerName));
			const std::string teamLabel = "Team:" + std::to_string(card.teamNumber + 1);
			windows().setAptText("LoadingScreen::TeamNumber" + n, loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, teamLabel)));
			windows().setAptText("LoadingScreen::ArmyName" + n, loadScreenU16ToUtf8(card.armyName));
		}
		else
		{
			// RW 0x81D0A4: an unused card's three texts are set to a single space (not empty: an empty record text lets the field show its default, the label)
			windows().setAptText("LoadingScreen::PlayerName" + n, " ");
			windows().setAptText("LoadingScreen::TeamNumber" + n, " ");
			windows().setAptText("LoadingScreen::ArmyName" + n, " ");
		}
	}
	// lane MENU-1: the Level icons are drawn by RenderImage clips without `_imageMap` named by their path below the root (an inference)
	windows().note("loadscreen-level", "[S-3506] the Level icons' RenderImage clips (UIClip.Level.<c>) take the image record UIClip/Level/<c> by their path "
									   "below the root: the RenderImage callback's name rule was not read");
	// every card starts at 0 (processProgress(slot, 0) per card, RW 0x81D062)
	for (int c = 0; c < MAX_LOAD_SLOTS; ++c)
	{
		if (m_env.loadScreen->cards[c].occupied)
		{
			m_pending.push_back({ c, 0 });
		}
	}
}

bool AptLoadScreen::callBar(int card, int percent)
{
	std::string error;
	if (!windows().invokeAS(level(), "SetBarTo", { std::to_string(card), std::to_string(percent) }, nullptr, &error))
	{
		windows().note("movie-function-missing", "LoadScreen SetBarTo: " + error);
		return false;
	}
	m_calls.push_back({ card, percent });
	return true;
}

void AptLoadScreen::flush()
{
	if (m_pending.empty() || !windows().isAptWindowLoaded(level()))
	{
		return;
	}
	std::vector<std::pair<int, int>> todo;
	todo.swap(m_pending);
	for (const auto &p : todo)
	{
		callBar(p.first, p.second);
	}
}

bool AptLoadScreen::setProgress(int card, int percent)
{
	if (!windows().isAptWindowLoaded(level()))
	{
		m_pending.push_back({ card, percent });
		return false;
	}
	flush();
	return callBar(card, percent);
}

bool AptLoadScreen::setLocalProgress(int percent)
{
	return setProgress(m_env.loadScreen ? m_env.loadScreen->localCard : 0, percent);
}

namespace
{
// RW 0xDB6A18: the Options screen's externs, in the provider's index order (RW 0x91F7F5)
const char *const kOptionsExterns[7] = { "MasterOption0Num", "MasterOption0Current", "MasterOption0ResetDefault", "AllowResolutionChange", "AllowAdvancedOptions",
	"NetworkEnabled", "AdvancedOnly" };
// RW 0xDA2298 (0x14-byte entries): the volume keys of OptionPreferences, by sound type
const char *const kVolumeKeys[5] = { "SFXVolume", "VoiceVolume", "MusicVolume", "AmbientVolume", "MovieVolume" };
// the gadget names of the volume sliders (InitGadgets: + 0x2DC SoundFx, + 0x2E0 Voice, + 0x2E4 Music, + 0x2E8 Ambient, + 0x2EC Movie)
const char *const kVolumeGadgets[5] = { "SoundFxVolume", "VoiceVolume", "MusicVolume", "AmbientVolume", "MovieVolume" };
// lane OPTS-4: the MiscAudio slots of the volume samples by sound type (TheAudio's MiscAudio + 0xA8 + 4 * type, read by RW 0x91F90D; the field table
// RW 0xBF5450 puts VolumeSampleSoundFX at 0xA8, Voice 0xAC, Music 0xB0, Ambient 0xB4, Movie 0xB8)
const char *const kVolumeSamples[5] = { "VolumeSampleSoundFX", "VolumeSampleVoice", "VolumeSampleMusic", "VolumeSampleAmbient", "VolumeSampleMovie" };
// the Detail list's entries (InitGadgets RW 0x9205C4: GUI:UltraHigh .. GUI:VeryLow, GUI:Custom, item data the level)
const std::pair<const char *, int> kDetailItems[6] = { { "GUI:UltraHigh", 4 }, { "GUI:High", 3 }, { "GUI:Medium", 2 }, { "GUI:Low", 1 }, { "GUI:VeryLow", 0 },
	{ "GUI:Custom", 5 } };

// RW 0xD9E760: GameLODManager's audio LOD names (index 0, 1)
const char *const kAudioLOD[2] = { "Low", "High" };
// the build's version (RW 0x644CC2 parses "VERSION=2.1.2614.37001" of the build string RW 0x644D8A with "%d.%d.%d.%d"; major + 0, minor + 4)
constexpr int kVersionMajor = 2, kVersionMinor = 1;

// the text setAptText takes (UTF-8) of a game text
std::string toUtf8(const std::u16string &text)
{
	std::string utf8;
	for (char16_t c : text)
	{
		if (c < 0x80)
		{
			utf8 += (char)c;
		}
		else if (c < 0x800)
		{
			utf8 += (char)(0xC0 | (c >> 6));
			utf8 += (char)(0x80 | (c & 0x3F));
		}
		else
		{
			utf8 += (char)(0xE0 | (c >> 12));
			utf8 += (char)(0x80 | ((c >> 6) & 0x3F));
			utf8 += (char)(0x80 | (c & 0x3F));
		}
	}
	return utf8;
}

bool equalsNoCase(const std::string &a, const char *b)
{
	std::size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return i == a.size() && b[i] == 0;
}

// ZH UnicodeString::format of a "%d"-style game text: %d / %s are replaced in order by the integers (the only conversions Version:Format2 uses)
std::u16string formatInts(const std::u16string &format, std::initializer_list<int> values)
{
	std::u16string out;
	auto it = values.begin();
	for (std::size_t i = 0; i < format.size(); ++i)
	{
		if (format[i] == u'%' && i + 1 < format.size())
		{
			std::size_t j = i + 1;
			while (j < format.size() && (format[j] == u'0' || format[j] == u'.' || (format[j] >= u'1' && format[j] <= u'9')))
			{
				++j;
			}
			if (j < format.size() && (format[j] == u'd' || format[j] == u'i' || format[j] == u'u'))
			{
				if (it != values.end())
				{
					out += asciiToU16(std::to_string(*it++));
				}
				i = j;
				continue;
			}
			if (format[i + 1] == u'%')
			{
				out += u'%';
				++i;
				continue;
			}
		}
		out += format[i];
	}
	return out;
}
} // namespace

std::u16string AptOptionsScreen::versionText(const GameTextSource *text)
{
	// RW 0x6446A1: TheGameText->fetch("Version:Format2") formatted with the major and minor
	std::u16string format;
	if (!text || !text->fetch("Version:Format2", format))
	{
		format = u"Version:Format2"; // a missing label shows as the label (the game text's own rule)
	}
	return formatInts(format, { kVersionMajor, kVersionMinor });
}

namespace
{
// lane OPTS-2: APT:CannotChangeResolution where the main menu opened the screen and the device cannot change the display mode [S-3401]
const char *const kResolutionElsewhere = "Use OpenBFME \xE2\x86\x92 Display.";
// lane OPTS-5: the page keys of the original game's settings
const char *const kRetailKeyPrefix = "Retail.";
} // namespace

namespace
{
// the basic page's three settings frames (Options.apt: Main.FrameLeft / FrameMiddle / FrameRight, dump_tree of the 2.01 movie) the page takes the
// place of, the subtitle it renames, the button it copies and the copy's name
const char *const kBasicFrames[3] = { "Main.FrameLeft", "Main.FrameMiddle", "Main.FrameRight" };
const char *const kSubtitlePath = "Main.SubTitle";
const char *const kEntrySourcePath = "Main.FrameLeft.Advanced";
const char *const kEntryName = "OpenBFMEEntry";
const char *const kEntryCaption = "OPENBFME";
const char *const kMirroredKeys[3] = { "ScrollFactor", "AllHealthBars", "AlternateMouseSetup" }; // the retail keys the page shows too
const char *const kBarButtons[3] = { "Buttons.Cancel", "Buttons.Default", "Buttons.Accept" }; // the basic page's bottom bar
const char *const kTooltipPrefix = "tooltips"; // the movie's invisible tooltip areas (tooltipsEAX3 ...): hidden with the frames
constexpr int kEntryScriptDepth = 9000;         // an ActionScript depth above every placed child of FrameLeft
} // namespace

AptOptionsScreen::AptOptionsScreen(WindowManager &windows, Shell &shell, ShellEnvironment &environment, ShellServices &services)
	: AptScreen(windows, shell, "Options.apt", "AptOptions"), m_env(environment), m_services(services)
{
	if (!m_env.options)
	{
		windows.note("options-unwired", "Options: no OptionPreferences: Save writes nothing [S-1484]");
	}
	// RW 0x816157 (the shared OnInitialized): the screen's state + 0x27C becomes 1, the basic page
	registerCommand("AptOptions::OnInitialized", [this](const std::string &) { m_state = 1; });
	// lane OPTS-3: while the OpenBFME page is up the bottom bar's commands are the page's (not retail: RotWK has no such page)
	registerCommand("AptOptions::Cancel", [this](const std::string &) {
		if (extensionOpen())
		{
			m_extensionEvents.push_back({ OpenBFMEPageEvent::Kind::Cancel, std::string(), 0 });
			return;
		}
		cancel();
	});
	registerCommand("AptOptions::Save", [this](const std::string &) {
		if (extensionOpen())
		{
			m_extensionEvents.push_back({ OpenBFMEPageEvent::Kind::Accept, std::string(), 0 });
			return;
		}
		save();
	});
	registerCommand("AptOptions::Reset", [this](const std::string &) {
		if (extensionOpen())
		{
			m_extensionEvents.push_back({ OpenBFMEPageEvent::Kind::Reset, std::string(), 0 });
			return;
		}
		reset();
	});
	windows.setMouseFilter([this](int kind, float x, float y) { return mouseFilter(kind, x, y); });
	windows.setCapturesMouse(true); // lane OPTS-5: Options covers the stage: over a game every press is its (the HUD asks, InGameHud::isOverGui)
	// lane OPTS-5: after every update of the movies (their timelines place the basic page's frames) the frames stay hidden until the menu is up, so no
	// frame of the retail page is ever drawn
	windows.addUpdateListener(this, [this]() {
		if (menuMode() && level() >= 0 && (!m_menuOpened || m_state == kExtensionState))
		{
			setExtensionHidden(true); // the movie's own timeline must not bring them (or its tooltip areas) back while the menu is up
		}
	});
	registerCommand("AptOptions::RefreshNat", unportedCommand("AptOptions::RefreshNat"));
	registerCommand("AptOptions::EnterAdvancedSettings", [this](const std::string &) { enterAdvancedSettings(); });
	registerScreenRef("AptOptions::InitGadgets", [this](const std::string &name, GameWindow *w) { initGadget(name, w); });
	for (int i = 0; i < 7; ++i)
	{
		registerProvider(kOptionsExterns[i], [this, i](const std::string &, std::string &value, bool setting) { return provide(i, value, setting); });
	}
	// lane PLAY-2: RW 0x921325 .. 0x9213F2: the preset templates (0 .. 4, Custom 5) and the advanced options' choice counts
	for (int i = 0; i <= GameLODManager::kCustomLevel; ++i)
	{
		const std::string name = i == GameLODManager::kCustomLevel ? std::string("MasterOption0TemplateCustom") : "MasterOption0Template" + std::to_string(i);
		registerProvider(name, [this, i](const std::string &, std::string &value, bool setting) { return provideTemplate(i, value, setting); });
	}
	for (int i = 0; i < GameLODManager::kAdvancedOptionCount; ++i)
	{
		// RW 0x91EA4E (decomp AptOptions::AdvancedOptionNum): a read answers the count; a write changes nothing
		registerProvider("AdvancedOption" + std::to_string(i) + "Num", [i](const std::string &, std::string &value, bool setting) {
			if (!setting)
			{
				value = std::to_string(GameLODManager::advancedOption(i).count);
			}
			return true;
		});
	}
	bindAdvancedLabels();
	// RW 0x9215E7: + 0x310 is the LOD manager's level, which its init takes from Options.ini's StaticGameLOD (OptionPreferences::getStaticGameDetail,
	// GetEnumValue over the six level names); without one the benchmark's level (not ported, S-2481) is replaced by Custom
	if (m_env.gameLOD && m_env.gameLOD->staticLevel() != -1)
	{
		m_masterOption = m_env.gameLOD->staticLevel(); // lane OPTS-2: the manager's level (its init ran at boot: GameLODManager::init)
	}
	else if (m_env.options && m_env.options->has("StaticGameLOD"))
	{
		const int level = GameLODManager::findLevel(m_env.options->get("StaticGameLOD"));
		if (level != -1)
		{
			m_masterOption = level;
		}
	}
	// RW 0x921586 .. 0x9215A6: APT:VersionNum is the version text
	windows.setAptText("APT:VersionNum", toUtf8(versionText(m_env.gameText)));
	// lane OPTS-2: the screen is retail's alone; the OpenBFME soft particle setting (UI-2) is the OpenBFME screen's (Graphics+, OpenBFMESoftParticles)
	windows.note("options-soft-particles", "[S-1484] Options: the OpenBFME soft particle setting is on the OpenBFME screen (Graphics+), saved as Options.ini's "
		+ std::string(OptionPreferences::kSoftParticles));
	windows.note("options-unported", "[S-1913] Options: the display mode list, the LOD manager's ideal level (MasterOption0ResetDefault) and RefreshNat are not ported");
	windows.note("options-advanced", m_env.gameLOD ? std::string("[S-2481] Options: the hardware detection behind the ideal level, the high-detail warnings and the '(level)' labels are not ported")
												  : std::string("[S-2481] Options: no GameLODManager: the level and custom settings are saved but not applied"));
}

void AptOptionsScreen::open(Shell &shell, bool networkEnabled, bool allowResolutionChange, bool allowAdvanced, bool advancedOnly)
{
	// RW 0x91ED91: only when no Options screen exists (the singleton RW 0xDEA370), push, then set the mode of the screen the push made
	if (shell.findScreenByFilename("Options.apt"))
	{
		return;
	}
	shell.push("Options.apt", false);
	shell.windows().setBackground(1); // RW 0x91EE08 .. 0x91EE0A: the front-end background
	if (AptOptionsScreen *screen = dynamic_cast<AptOptionsScreen *>(shell.findScreenByFilename("Options.apt")))
	{
		screen->setMode(networkEnabled, allowResolutionChange, allowAdvanced, advancedOnly);
	}
}

void AptOptionsScreen::setMode(bool networkEnabled, bool allowResolutionChange, bool allowAdvanced, bool advancedOnly)
{
	m_networkEnabled = networkEnabled;
	m_allowResolution = allowResolutionChange && m_env.resolutionChangeApplies; // lane OPTS-2 [S-3401]
	if (allowResolutionChange && !m_env.resolutionChangeApplies)
	{
		windows().note("options-resolution", "[S-3401] Options: the Resolution list is disabled (AllowResolutionChange 0): the device does not change the display mode; "
			"the OpenBFME screen's Display tab sets the window");
	}
	// lane OPTS-2 (coordinator review): the movie shows APT:CannotChangeResolution in place of the list; retail's text ("only from the Main Menu Settings")
	// is wrong where the main menu opened the screen, so there it names the OpenBFME screen; another opening shows the game text
	if (allowResolutionChange && !m_env.resolutionChangeApplies)
	{
		windows().setAptText("APT:CannotChangeResolution", kResolutionElsewhere);
	}
	else if (m_env.gameText)
	{
		std::u16string text;
		if (m_env.gameText->fetch("APT:CannotChangeResolution", text))
		{
			windows().setAptText("APT:CannotChangeResolution", toUtf8(text));
		}
	}
	m_allowAdvanced = allowAdvanced;
	m_advancedOnly = advancedOnly;
	if (menuMode())
	{
		// lane OPTS-5: the main menu's CUSTOM SETTINGS (AdvancedOnly) opens the menu on its detail settings; the movie itself stays on its basic page
		// (the one the menu replaces), so its custom settings page never shows
		m_openOnDetail = advancedOnly;
		m_advancedOnly = false;
	}
}

bool AptOptionsScreen::provide(int index, std::string &value, bool setting)
{
	// RW 0x91F7F5 (a read answers; only MasterOption0Current takes a write)
	if (setting)
	{
		if (index == 1)
		{
			m_masterOption = equalsNoCase(value, "Custom") ? 5 : std::atoi(value.c_str());
			selectDetail(); // RW 0x91F8A3 -> RW 0x91EB30 (the numeric level's warning, RW 0x91F790, is not ported: S-2481)
		}
		return true;
	}
	switch (index)
	{
		case 0: value = "5"; return true;
		case 1: value = m_masterOption == 5 ? "Custom" : std::to_string(m_masterOption); return true;
		case 2:
			// the LOD manager's + 0x17C4 (not ported): reported, the extern answers nothing
			windows().note("provider-unwired", "MasterOption0ResetDefault: the LOD manager [S-1913]");
			return false;
		case 3: value = m_allowResolution ? "1" : "0"; return true;
		case 4: value = m_allowAdvanced ? "1" : "0"; return true;
		case 5: value = m_networkEnabled ? "1" : "0"; return true;
		case 6: value = m_advancedOnly ? "1" : "0"; return true; // lane OPTS-5: false in the menu (setMode)
		default: return false;
	}
}

int AptOptionsScreen::initialVolume(int type) const
{
	// RW 0x6E5FB3 then the ftol at RW 0x920A11
	float v = 0.0f;
	const std::string key = kVolumeKeys[type];
	if (m_env.options && m_env.options->has(key))
	{
		v = (float)std::atof(m_env.options->get(key).c_str());
		if (v < 0.0f)
		{
			v = 0.0f;
		}
	}
	else
	{
		if (!m_env.haveDefaultVolumes)
		{
			const_cast<WindowManager &>(const_cast<AptOptionsScreen *>(this)->windows()).note("options-unwired", std::string(kVolumeKeys[type]) + ": no AudioSettings defaults were given");
		}
		v = m_env.defaultVolumes[type] * 100.0f;
	}
	return (int)v;
}

int AptOptionsScreen::initialBrightness() const
{
	// RW 0x6E5ECD: atoi, missing 50.0 (RW 0xBD88C4)
	if (m_env.options && m_env.options->has("Brightness"))
	{
		return std::atoi(m_env.options->get("Brightness").c_str());
	}
	return 50;
}

int AptOptionsScreen::initialScrollSpeed() const
{
	// RW 0x6E59A9 * 50.0 (RW 0x920A0B), ftol
	float factor;
	if (m_env.options && m_env.options->has("ScrollFactor"))
	{
		int v = std::atoi(m_env.options->get("ScrollFactor").c_str());
		if (v < 0)
		{
			v = 1;
		}
		if (v > 100)
		{
			v = 100;
		}
		factor = (float)v * 0.02f;
	}
	else
	{
		factor = m_env.keyboardDefaultScrollSpeedFactor;
	}
	return (int)(factor * 50.0f);
}

GameWindow *AptOptionsScreen::gadget(const std::string &name) const
{
	auto it = m_gadgets.find(name);
	return it == m_gadgets.end() ? nullptr : it->second;
}

bool AptOptionsScreen::checked(const std::string &name) const
{
	GameWindow *w = gadget(name);
	return w && GadgetCheckBoxIsChecked(w);
}

int AptOptionsScreen::sliderValue(const std::string &name) const
{
	GameWindow *w = gadget(name);
	return w ? GadgetSliderGetPosition(w) : -1; // RW 0x91EA1D
}

void AptOptionsScreen::initGadget(const std::string &fullName, GameWindow *w)
{
	// RW 0x9205C4
	if (!w)
	{
		return;
	}
	const std::string prefix = "Options::";
	if (fullName.compare(0, prefix.size(), prefix) != 0)
	{
		windows().note("unported-gadget-init", "AptOptions::InitGadgets(" + fullName + ")");
		return;
	}
	const std::string name = fullName.substr(prefix.size());
	m_gadgets[name] = w;
	const OptionPreferences *prefs = m_env.options;
	auto yes = [&](const char *key, bool missing) { return prefs && prefs->has(key) ? prefs->get(key) == "yes" : missing; };
	if (name == "Resolution")
	{
		// the display modes "%dx%d"; Options.ini's "Resolution" ("%d%d") is selected; the device's size when it has none (the LOD manager's default
		// 800 x 600 / 1024 x 768 is not ported: S-1913)
		int wantW = m_env.currentResolution.first, wantH = m_env.currentResolution.second;
		if (prefs && prefs->has("Resolution"))
		{
			int a = 0, b = 0;
			if (std::sscanf(prefs->get("Resolution").c_str(), "%d%d", &a, &b) == 2)
			{
				wantW = a;
				wantH = b;
			}
		}
		GadgetComboBoxReset(w);
		int selected = -1;
		for (std::size_t i = 0; i < m_env.displayModes.size(); ++i)
		{
			const auto &m = m_env.displayModes[i];
			GadgetComboBoxAddEntry(w, asciiToU16(std::to_string(m.first) + "x" + std::to_string(m.second)), GameMakeColor(255, 255, 255, 255));
			if (m.first == wantW && m.second == wantH)
			{
				selected = (int)i;
			}
		}
		if (m_env.displayModes.empty())
		{
			windows().note("options-unwired", "Options::Resolution: the device lists no display modes");
		}
		GadgetComboBoxSetSelectedPos(w, selected);
		if (!m_allowResolution)
		{
			w->winEnable(false);
		}
		return;
	}
	if (name == "Detail")
	{
		// GUI:UltraHigh 4, GUI:High 3, GUI:Medium 2, GUI:Low 1, GUI:VeryLow 0, GUI:Custom 5; the current one selected (RW 0x91EB30)
		const auto &kDetail = kDetailItems;
		GadgetComboBoxReset(w);
		for (int i = 0; i < 6; ++i)
		{
			std::u16string text;
			if (!m_env.gameText || !m_env.gameText->fetch(kDetail[i].first, text))
			{
				text = asciiToU16(kDetail[i].first);
			}
			const int index = GadgetComboBoxAddEntry(w, text, GameMakeColor(255, 255, 255, 255));
			GadgetComboBoxSetItemData(w, index, reinterpret_cast<void *>((std::uintptr_t)kDetail[i].second));
		}
		m_detailRefreshing = true; // RW 0x91EB30
		for (int i = 0; i < 6; ++i)
		{
			if ((int)(std::uintptr_t)GadgetComboBoxGetItemData(w, i) == m_masterOption)
			{
				GadgetComboBoxSetSelectedPos(w, i, false);
				break;
			}
		}
		m_detailRefreshing = false;
		if (!m_allowAdvanced)
		{
			w->winEnable(false);
		}
		return;
	}
	for (int t = 0; t < 5; ++t)
	{
		if (name == kVolumeGadgets[t])
		{
			GadgetSliderSetPosition(w, initialVolume(t)); // lane OPTS-4: the Movie slider is live too (the device has the MOVIE submix slider)
			return;
		}
	}
	if (name == "Brightness")
	{
		GadgetSliderSetPosition(w, initialBrightness());
		return;
	}
	if (name == "ScrollSpeed")
	{
		GadgetSliderSetPosition(w, initialScrollSpeed());
		return;
	}
	if (name == "HealthBars")
	{
		// RW 0x920A4A -> RW 0x91EBD1 (lane OPTS-2): checked from AllHealthBars while the detail allows the box, else checked and disabled
		refreshHealthBarsBox(w);
		return;
	}
	if (name == "AlternateMouseSetUp")
	{
		// RW 0x6E61D4 answers "not yes" (missing: GlobalData + 0x5C, 0 by the constructor: INFERENCE); the box is its negation (RW 0x920A80)
		GadgetCheckBoxSetChecked(w, yes("AlternateMouseSetup", true));
		return;
	}
	if (name == "OnlineIp")
	{
		// the machine's addresses (RW 0x719C53 / 0x9122FC), item data the address; GameSpyIPAddress's is selected (RW 0x6E65E6: missing or unknown:
		// GlobalData + 0xA48, nothing here)
		const std::string want = prefs && prefs->has("GameSpyIPAddress") ? prefs->get("GameSpyIPAddress") : std::string();
		GadgetComboBoxReset(w);
		int selected = -1;
		for (const auto &a : m_env.localAddresses)
		{
			const int index = GadgetComboBoxAddEntry(w, asciiToU16(a.first), GameMakeColor(255, 255, 255, 255));
			GadgetComboBoxSetItemData(w, index, reinterpret_cast<void *>((std::uintptr_t)a.second));
			if (equalsNoCase(want, a.first.c_str()))
			{
				selected = index;
			}
		}
		if (selected < 0 && !m_env.localAddresses.empty())
		{
			windows().note("options-ip", "GameSpyIPAddress '" + want + "' is not an address of this machine: none selected (GlobalData + 0xA48: S-1913)");
		}
		GadgetComboBoxSetSelectedPos(w, selected);
		if (selected < 0)
		{
			GadgetComboBoxSetText(w, std::u16string());
		}
		if (!m_networkEnabled)
		{
			w->winEnable(false);
		}
		return;
	}
	if (name == "OnlinePortNum")
	{
		// 5 characters (RW 0x81606D(entry, 5)); FirewallPortOverride ("%d" when not 0, else empty, RW 0x920BF4 .. 0x920C57)
		if (EntryData *entry = static_cast<EntryData *>(w->winGetUserData()))
		{
			entry->maxTextLen = 5;
		}
		const int port = prefs && prefs->has("FirewallPortOverride") ? (std::atoi(prefs->get("FirewallPortOverride").c_str()) & 0xFFFF) : 0;
		GadgetTextEntrySetText(w, port > 0 ? asciiToU16(std::to_string(port)) : std::u16string());
		if (!m_networkEnabled)
		{
			w->winEnable(false);
		}
		return;
	}
	if (name == "Firewall")
	{
		return; // RW 0x920C61: nothing
	}
	if (name == "SendDelay" || name == "TurnOffMessengerInGame")
	{
		GadgetCheckBoxSetChecked(w, yes(name.c_str(), false)); // GlobalData + 0xA50 / + 0xB69 when missing (0: INFERENCE)
		if (!m_networkEnabled)
		{
			w->winEnable(false);
		}
		return;
	}
	if (name == "DisplayForeignLanguage")
	{
		GadgetCheckBoxSetChecked(w, yes("DisplayForeignLanguage", false)); // GlobalData + 0xB6D when missing (0: INFERENCE)
		return;
	}
	if (name == "FilterLanguage")
	{
		GadgetCheckBoxSetChecked(w, yes("LanguageFilter", false)); // GlobalData + 0xB68 when missing (0: INFERENCE)
		return;
	}
	if (name == "EAX3")
	{
		GadgetCheckBoxSetChecked(w, yes("UseEAX3", false));
		w->winEnable(false); // lane OPTS-2 [S-1913]: no EAX 3 device
		return;
	}
	if (name == "HighAudioQuality")
	{
		// AudioLOD's index (RW 0x6025F1, case-insensitive: Low 0, High 1; missing -1): checked when 1
		const bool high = prefs && prefs->has("AudioLOD") && equalsNoCase(prefs->get("AudioLOD"), kAudioLOD[1]);
		GadgetCheckBoxSetChecked(w, high);
		w->winEnable(false); // lane OPTS-2 [S-2481]: GameLOD.ini's AudioLOD is not ported
		return;
	}
	windows().note("unported-gadget-init", "AptOptions::InitGadgets(" + fullName + ")");
}

void AptOptionsScreen::reset()
{
	// RW 0x91F4D1 (the basic page; lane OPTS-5: the menu, which shows its settings)
	if (basicLive())
	{
		if (GameWindow *b = gadget("Brightness"))
		{
			int mn = 0, mx = 0;
			GadgetSliderGetMinMax(b, &mn, &mx);
			GadgetSliderSetPosition(b, (mx - mn) / 2 + mn);
			// lane OPTS-5: RW 0x91F56C .. 0x91F5F3: the slider's position becomes the display's gamma at once (TheDisplay vslot 0x64)
			const int v = GadgetSliderGetPosition(b);
			if (v != -1)
			{
				m_services.setDisplayGamma(OptionPreferences::displayGamma(v, 1.0f));
			}
		}
		if (GameWindow *sc = gadget("ScrollSpeed"))
		{
			GadgetSliderSetPosition(sc, (int)(m_env.keyboardDefaultScrollSpeedFactor * 50.0f));
		}
		for (const char *box : { "AlternateMouseSetUp", "SendDelay", "DisplayForeignLanguage", "TurnOffMessengerInGame", "EAX3" })
		{
			if (GameWindow *w = gadget(box))
			{
				GadgetCheckBoxSetChecked(w, false);
			}
		}
		if (GameWindow *port = gadget("OnlinePortNum"))
		{
			GadgetTextEntrySetText(port, std::u16string());
		}
		if (gadget("HighAudioQuality"))
		{
			windows().note("options-unported", "Reset: HighAudioQuality takes the LOD manager's recommended audio LOD (RW 0x602021) [S-1913]");
		}
		for (int t = 0; t < 5; ++t)
		{
			if (GameWindow *w = gadget(kVolumeGadgets[t]))
			{
				// lane OPTS-4: RW 0x91F4D1 sets the slider and the volume itself (TheAudio vslot 0xE8(type, value / 100)): GSM_SET_SLIDER sends no track
				const int value = (int)(m_env.defaultVolumes[t] * 100.0f);
				GadgetSliderSetPosition(w, value);
				applyVolume(t, value);
			}
		}
		refreshHealthBarsBox(gadget("HealthBars")); // RW 0x91F754 -> RW 0x91EBD1
	}
}

void AptOptionsScreen::cancel()
{
	// RW 0x91EC52: the saved volumes are applied again (TheAudio vslot 0xE8(type, value * 0.01)), then the screen closes (RW 0x91EA39)
	if (m_state == 2 && !m_advancedOnly)
	{
		m_state = 1;
		return;
	}
	// lane OPTS-5: RW 0x91EC8C .. 0x91ECB8: the display's gamma goes back to the saved one (GlobalData + 0xBCC: the brightness Options.ini holds)
	m_services.setDisplayGamma(OptionPreferences::displayGamma(m_env.options ? (int)m_env.options->getBrightness() : 50, 1.0f));
	for (int t = 0; t < 5; ++t)
	{
		applyVolume(t, initialVolume(t));
	}
	windows().requestShellPop();
}

// TheAudio vslot 0xE8(type, value / 100) as the device takes it: the Options.ini entry of the type with the slider's value ("%f")
void AptOptionsScreen::applyVolume(int type, int value)
{
	char buf[64];
	std::snprintf(buf, sizeof buf, "%f", (double)(float)value);
	m_services.applyOption(kVolumeKeys[type], buf);
}

// Lane OPTS-4: RotWK's gadget messages of the volume sliders, RW 0x91F90D (identical to BFME2 0x00919167, decomp AptOptionsRva00519167.cpp).
// TARGET FACTS: the track message (RotWK's 0x400C) of a volume slider with a position other than -1 sets that type's volume at once (TheAudio vslot
// 0xE8(type, position / 100)); a sample of another type than the last one (the screen keeps the previewed type, 5 = none) is removed first (removeAudioEvent);
// then, unless the kept handle still sounds (isCurrentlyPlaying), the type's MiscAudio sample is added (addAudioEvent) and its handle kept, except the
// music sample, which is added only when TheGameLogic exists, the game is not a multiplayer game (RW 0x441B7C) and the screen's + 0x280 is 0 (the
// screen was not opened from the main menu, where the shell's own music plays). So a sample does not restart while it sounds and starts again once it
// has ended, as long as the slider moves. The slider's 0x4010 (a track click done, the thumb let go) removes the sample and forgets the type, unless
// the type is 1: the voice line plays to its end.
// The sample's event is built with the view type 2 (the constructor RW 0x6DA95E stores it at + 0x30): heard in either view.
// INFERENCE: "TheGameLogic exists" is the port's "a game is running" (ShellEnvironment::quitMenu, lane END-2), so the music sample plays on the quit
// menu's Options screen of a skirmish or campaign game only.
void AptOptionsScreen::trackVolume(int type, int value)
{
	if (value == -1)
	{
		return;
	}
	applyVolume(type, value);
	if (m_previewSlider != type)
	{
		m_services.removeAudioEvent(m_previewHandle);
		m_previewHandle = 0;
		m_previewSlider = type;
	}
	if (!m_services.isCurrentlyPlaying(m_previewHandle))
	{
		m_previewHandle = 0;
		const QuitMenuContext *game = m_env.quitMenu; // TheGameLogic (lane END-2: null without a game)
		if (type != 2 || (game && game->inGame && !game->isMultiplayer() && !m_allowResolution))
		{
			m_previewHandle = m_services.addMiscAudioEvent(kVolumeSamples[type], 2); // RW 0x91F90D: the event's view type 2 (AudioEventRTS.h VIEW_ANY)
		}
	}
}

void AptOptionsScreen::sliderClicked()
{
	if (m_previewHandle && m_previewSlider != 1)
	{
		m_services.removeAudioEvent(m_previewHandle);
		m_previewHandle = 0;
		m_previewSlider = 5;
	}
}

AptOptionsScreen::~AptOptionsScreen()
{
	// lane OPTS-3: nothing of this screen may run after it is gone
	windows().setMouseFilter(nullptr);
	windows().setCapturesMouse(false);
	if (AptGadgetLayer *layer = windows().gadgetLayer())
	{
		layer->setDecorator(nullptr);
	}
	else if (m_page)
	{
		m_page->abandon(); // the layer (and every gadget it made) is gone already
	}
	m_page.reset();
}

void AptOptionsScreen::runInit()
{
	AptScreen::runInit();
}

void AptOptionsScreen::runShutdown(bool *immediate)
{
	AptScreen::runShutdown(immediate);
}

WindowMsgHandledType AptOptionsScreen::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	if (m_page && m_page->message(msg, data1, data2))
	{
		return MSG_HANDLED; // lane OPTS-3: one of the OpenBFME page's gadgets
	}
	GameWindow *detail = gadget("Detail");
	if (msg == GCM_SELECTED && detail && !m_detailRefreshing && reinterpret_cast<GameWindow *>(data1) == detail)
	{
		// lane PLAY-2, RotWK's twin of BFME2 0x519167 (decomp AptOptionsRva00519167.cpp): Custom (item data 5) opens the advanced page through the
		// movie's ShowAdvancedSettings; another level becomes + 0x310 (its warning, RW 0x91F790, is not ported: S-2481) and the page refreshes (RW 0x91EBD1)
		int index = -1;
		GadgetComboBoxGetSelectedPos(detail, &index);
		const int chosen = index >= 0 ? (int)(std::uintptr_t)GadgetComboBoxGetItemData(detail, index) : -1;
		if (chosen == GameLODManager::kCustomLevel)
		{
			std::string error;
			if (!windows().invokeAS(level(), "ShowAdvancedSettings", {}, nullptr, &error))
			{
				windows().note("options-advanced", "ShowAdvancedSettings: " + error);
			}
		}
		else if (chosen >= 0)
		{
			m_masterOption = chosen;
			refreshHealthBarsBox(gadget("HealthBars")); // RW 0x91F9CE .. 0x91F9D4
		}
		AptScreen::gadgetMessage(from, msg, data1, data2);
		return MSG_HANDLED;
	}
	if (msg == GSM_SLIDER_TRACK)
	{
		// lane OPTS-4, RW 0x91F90D; lane OPTS-5: the gamma slider's track sets the display's gamma at once (stop S-4510 closed)
		const GameWindow *control = reinterpret_cast<GameWindow *>(data1);
		if (control && control == gadget("Brightness") && (int)data2 != -1)
		{
			m_services.setDisplayGamma(OptionPreferences::displayGamma((int)data2, 1.0f));
		}
		for (int t = 0; t < 5; ++t)
		{
			if (control && control == gadget(kVolumeGadgets[t]))
			{
				trackVolume(t, (int)data2);
				break;
			}
		}
		AptScreen::gadgetMessage(from, msg, data1, data2);
		return MSG_HANDLED;
	}
	if (msg == GSM_SLIDER_CLICKED)
	{
		sliderClicked();
	}
	return AptScreen::gadgetMessage(from, msg, data1, data2);
}

// lane PLAY-2: RW 0x9213F4 .. 0x921580 (TheGameText vslot 0x38 fetch, then the Apt text record RW 0x624FFD)
void AptOptionsScreen::bindAdvancedLabels()
{
	for (int i = 0; i <= GameLODManager::kCustomLevel; ++i)
	{
		const std::string name = i == GameLODManager::kCustomLevel ? std::string("APT:MasterOption0_Custom") : "APT:MasterOption0_" + std::to_string(i);
		windows().setAptText(name, toUtf8(fetchOrMissing(m_env.gameText, std::string("APT:MasterOption_") + GameLODManager::levelName(i))));
	}
	for (int i = 0; i < GameLODManager::kAdvancedOptionCount; ++i)
	{
		const AdvancedOptionInfo &o = GameLODManager::advancedOption(i);
		windows().setAptText("APT:AdvancedOption" + std::to_string(i), toUtf8(fetchOrMissing(m_env.gameText, std::string("APT:AdvancedOption_") + o.key)));
		for (int j = 0; j < o.count; ++j)
		{
			std::string label = std::string("APT:AdvancedOption_") + o.key + "_" + o.choices[j];
			if (label == "APT:AdvancedOption_ShadowLOD_UltraHigh")
			{
				label = "APT:AdvancedOption_ShaderLOD_UltraHigh"; // RW 0x921515 .. 0x92152E
			}
			windows().setAptText("APT:AdvancedOption" + std::to_string(i) + "_" + std::to_string(j), toUtf8(fetchOrMissing(m_env.gameText, label)));
		}
	}
}

std::string AptOptionsScreen::presetTemplate(int preset)
{
	if (preset == GameLODManager::kCustomLevel)
	{
		// INFERENCE (S-2481): the LOD manager's Custom record is the player's saved custom settings (RW 0x6019D1 fills it from Options.ini)
		return m_env.options ? AdvancedOptionPrefs::format(*m_env.options) : AdvancedOptionPrefs::format(std::array<int, GameLODManager::kAdvancedOptionCount>{});
	}
	if (!m_env.gameLOD)
	{
		windows().note("options-advanced", "[S-2481] Options: no GameLOD.ini presets: the level templates are the zeroed records");
		return AdvancedOptionPrefs::format(GameLODManager().presetSettings(preset));
	}
	return AdvancedOptionPrefs::format(m_env.gameLOD->presetSettings(preset)); // RW 0x601BBD into a fresh OptionPreferences, then RW 0x91EE9A
}

bool AptOptionsScreen::provideTemplate(int preset, std::string &value, bool setting)
{
	// RW 0x91F3B3
	if (setting)
	{
		if (preset == GameLODManager::kCustomLevel)
		{
			m_customTemplate = value; // the warning against + 0x308 (RW 0x91F2AB, APT:WarnHighGraphicDetail) is not ported: S-2481
		}
		return true;
	}
	value = preset == GameLODManager::kCustomLevel ? m_customTemplate : presetTemplate(preset);
	return true;
}

void AptOptionsScreen::enterAdvancedSettings()
{
	// RW 0x91F043
	m_state = 2;
	if (m_masterOption == -1)
	{
		return;
	}
	m_customTemplate = presetTemplate(m_masterOption);
}

void AptOptionsScreen::selectDetail()
{
	// RW 0x91EB30 (decomp Rva00518359Combo.cpp, tier A): the combo pointer (+ 0x2B0) is cleared while the item whose data is + 0x310 is selected, so the
	// selection message does not reach the Detail handler
	GameWindow *w = gadget("Detail");
	if (!w)
	{
		return;
	}
	m_detailRefreshing = true;
	for (int i = 0; i < 6; ++i)
	{
		if ((int)(std::uintptr_t)GadgetComboBoxGetItemData(w, i) == m_masterOption)
		{
			GadgetComboBoxSetSelectedPos(w, i, false);
			break;
		}
	}
	m_detailRefreshing = false;
}

void AptOptionsScreen::saveAdvanced(OptionPreferences &prefs)
{
	// RW 0x920500 .. 0x92058E: a level in [0, 6); Custom: + 0x314 into the nine keys (RW 0x91EE11) and the Custom record from them (RW 0x6019D1),
	// else the keys go (RW 0x6E6986); the LOD manager takes the level (RW 0x6020B4, lane OPTS-2: GameLODManager::setStaticLODLevel) and, when that
	// applied it, StaticGameLOD is written (RW 0x92056A .. 0x92057A, RW 0x6E6798); the device then draws with the new state ("StaticGameLOD" option)
	GameLODManager *lod = m_env.gameLOD;
	if (m_masterOption == GameLODManager::kCustomLevel)
	{
		AdvancedOptionPrefs::parseInto(m_customTemplate, prefs);
		if (lod)
		{
			lod->setCustomFromPreferences(prefs);
		}
	}
	else
	{
		AdvancedOptionPrefs::clearSettings(prefs);
	}
	if (!lod)
	{
		prefs.set("StaticGameLOD", GameLODManager::levelName(m_masterOption)); // no manager (it says so: options-advanced)
		return;
	}
	if (lod->setStaticLODLevel(m_masterOption, prefs))
	{
		prefs.set("StaticGameLOD", GameLODManager::levelName(lod->staticLevel()));
		m_services.applyOption("StaticGameLOD", prefs.get("StaticGameLOD"));
	}
}

void AptOptionsScreen::save()
{
	// RW 0x91FC9C: the basic page's entries (+ 0x27C == 1), written, then the screen closes (RW 0x91EA39)
	OptionPreferences *prefs = m_env.options;
	if (basicLive() && prefs)
	{
		if (m_allowResolution && gadget("Resolution"))
		{
			windows().note("options-unported", "Save: a resolution change is not applied [S-1913]");
		}
		const int brightness = sliderValue("Brightness");
		if (brightness != -1)
		{
			prefs->set("Brightness", std::to_string(brightness)); // "%d"; the gamma (TheGlobalData + 0xBCC) is the device's: S-1913
			m_services.applyOption("Brightness", std::to_string(brightness));
		}
		int scroll = sliderValue("ScrollSpeed");
		if (scroll != -1)
		{
			if (scroll <= 0)
			{
				scroll = 1;
			}
			prefs->set("ScrollFactor", std::to_string(scroll)); // GlobalData + 0xA9C / + 0xAA0 = scroll * 0.02
			m_services.applyOption("ScrollFactor", std::to_string(scroll));
		}
		for (int t = 0; t < 5; ++t)
		{
			const int v = sliderValue(kVolumeGadgets[t]);
			if (v != -1)
			{
				char buf[64];
				std::snprintf(buf, sizeof buf, "%f", (double)(float)v); // RW 0x6E68F6: "%f" of the float
				prefs->set(kVolumeKeys[t], buf);
				m_services.applyOption(kVolumeKeys[t], buf);
			}
		}
		if (gadget("HealthBars") && healthBarsAllowed())
		{
			prefs->setYesNo("AllHealthBars", checked("HealthBars")); // while the detail allows the box (RW 0x91EB77, lane OPTS-2)
			m_services.applyOption("AllHealthBars", prefs->get("AllHealthBars"));
		}
		if (gadget("AlternateMouseSetUp"))
		{
			prefs->setYesNo("AlternateMouseSetup", checked("AlternateMouseSetUp"));
		}
		if (gadget("EAX3"))
		{
			prefs->setYesNo("UseEAX3", checked("EAX3"));
		}
		if (gadget("HighAudioQuality"))
		{
			prefs->set("AudioLOD", kAudioLOD[checked("HighAudioQuality") ? 1 : 0]); // RW 0x6E6896 -> RW 0x601FF9
		}
		if (gadget("SendDelay"))
		{
			prefs->setYesNo("SendDelay", checked("SendDelay"));
		}
		if (GameWindow *ip = gadget("OnlineIp"))
		{
			int index = -1;
			GadgetComboBoxGetSelectedPos(ip, &index);
			if (index >= 0)
			{
				const std::uint32_t a = (std::uint32_t)(std::uintptr_t)GadgetComboBoxGetItemData(ip, index);
				prefs->set("GameSpyIPAddress", std::to_string(a >> 24) + "." + std::to_string((a >> 16) & 0xFF) + "." + std::to_string((a >> 8) & 0xFF) + "." +
					std::to_string(a & 0xFF)); // RW 0x6E6709
			}
		}
		if (GameWindow *port = gadget("OnlinePortNum"))
		{
			// _wtoi of the entry; 8088 .. 65534 is kept, anything else is 0 (RW 0x9202F2 ..); the NAT refresh that follows a change is not ported (S-1913)
			const std::u16string text = GadgetTextEntryGetText(port);
			std::string ascii;
			for (char16_t c : text)
			{
				ascii += c < 0x80 ? (char)c : '?';
			}
			int value = text.empty() ? 0 : std::atoi(ascii.c_str());
			if (!(value > 0x1F97 && value < 0xFFFF))
			{
				value = 0;
			}
			prefs->set("FirewallPortOverride", std::to_string(value & 0xFFFF));
		}
	}
	// RW 0x9204D9 .. 0x9204E1: the basic page's entries are written and the screen closes (RW 0x91EA39)
	bool closed = false;
	if (basicLive())
	{
		if (prefs)
		{
			writeOptions();
		}
		windows().requestShellPop();
		closed = true;
	}
	else if (m_state != 2)
	{
		windows().note("options-save", "Save before OnInitialized: the basic page is not written (RW 0x91FCB2)");
	}
	// lane PLAY-2: RW 0x9204F3 .. 0x9205AA, whatever the page: with AllowAdvancedOptions the level and the custom settings are written, then the screen
	// closes with AdvancedOnly (RW 0x9205A3; once here: the basic page's close above already asked for the pop) or goes back to its basic page
	if (m_allowAdvanced)
	{
		if (prefs && m_masterOption > -1 && m_masterOption < GameLODManager::kLevelCount)
		{
			saveAdvanced(*prefs);
			writeOptions(); // RW 0x920582 (RW 0x7B274C)
		}
		refreshHealthBarsBox(gadget("HealthBars")); // RW 0x920595
		if (m_advancedOnly)
		{
			if (!closed)
			{
				windows().requestShellPop();
			}
		}
		else
		{
			m_state = 1;
		}
	}
}

void AptOptionsScreen::frameUpdate()
{
	// lane OPTS-2 [S-3402]: on the advanced page the rows of the options the device does not draw yet are greyed and take no input (retail greys what
	// does not apply). A row is found by the label the screen binds to it (APT:AdvancedOption<i>, RW 0x9213F4): the text field showing "$AdvancedOption<i>"
	// (its bound variable read on its parent timeline); the clip holding that field is the row: _alpha 40 and every button / clip under it disabled
	updateEntry(); // lane OPTS-3
	menuUpdate();  // lane OPTS-5
	if (m_state == kExtensionState && m_page)
	{
		// lane OPTS-3: the subtitle names the page (every text field of Main.SubTitle: the movie keeps one per page; a frame of the movie must not
		// bring "SETTINGS" back)
		const std::string wanted = toUtf8(m_page->spec().subtitle);
		for (AptTextInst *subtitle : subtitleTexts())
		{
			if (!wanted.empty())
			{
				setFieldText(subtitle, wanted);
			}
		}
	}
	if (m_state != 2 || level() < 0)
	{
		return;
	}
	AptSpriteInst *root = windows().apt().level(level());
	if (!root)
	{
		return;
	}
	std::vector<AptCharacterInst *> stack{ root };
	while (!stack.empty())
	{
		AptCharacterInst *inst = stack.back();
		stack.pop_back();
		if (AptSpriteInst *sprite = inst->asSprite())
		{
			for (AptCharacterInst *c : sprite->children())
			{
				stack.push_back(c);
			}
			continue;
		}
		if (inst->type() != AptCharacterInst::Type::EditText || !inst->parent())
		{
			continue;
		}
		AptTextInst *text = static_cast<AptTextInst *>(inst);
		std::string shown = text->text;
		AptValue v;
		if (!text->variable.empty() && text->variable.find_first_of("./") == std::string::npos && text->parent()->getMember(text->variable, v) && !v.isUndefined())
		{
			shown = v.toString();
		}
		static const std::string prefix = "$AdvancedOption";
		if (shown.size() != prefix.size() + 1 || shown.compare(0, prefix.size(), prefix) != 0 || !std::isdigit((unsigned char)shown.back()))
		{
			continue;
		}
		const int index = shown.back() - '0';
		if (index >= GameLODManager::kAdvancedOptionCount || GameLODManager::advancedOptionDrawn(index))
		{
			continue;
		}
		AptCharacterInst *row = text->parent();
		row->setProperty("_alpha", AptValue::number(40.0));
		std::vector<AptCharacterInst *> below{ row };
		while (!below.empty())
		{
			AptCharacterInst *b = below.back();
			below.pop_back();
			b->enabled = false;
			if (AptSpriteInst *s = b->asSprite())
			{
				for (AptCharacterInst *c : s->children())
				{
					below.push_back(c);
				}
			}
		}
		m_greyedRows.insert(index);
	}
}

bool AptOptionsScreen::healthBarsAllowed() const
{
	// RW 0x91EB77 (BFME2 decomp Rva005183A0Finish.cpp, tier A): level 0 (VeryLow) no; Custom: the saved DecalLOD (RW 0x6E56EB, GetEnumValue row 8)
	// is not Off; another level yes
	if (m_masterOption == 0)
	{
		return false;
	}
	if (m_masterOption == GameLODManager::kCustomLevel)
	{
		// lane OPTS-5: in the menu the custom settings are pending until ACCEPT; RotWK saves them at its custom page's DONE (RW 0x9204F3) before the
		// box is judged again (RW 0x920595), so the menu judges by the pending DecalLOD
		if (menuMode())
		{
			return shownAdvancedSettings()[8] != 0;
		}
		return m_env.options && AdvancedOptionPrefs::getSetting(*m_env.options, 8) != 0;
	}
	return true;
}

void AptOptionsScreen::refreshHealthBarsBox(GameWindow *box)
{
	// RW 0x91EBD1: + 0x2BC is the box
	if (!box)
	{
		return;
	}
	if (!healthBarsAllowed())
	{
		GadgetCheckBoxSetChecked(box, true); // RW 0x91EBF3 .. 0x91EC04
		box->winEnable(false);
	}
	else
	{
		GadgetCheckBoxSetChecked(box, m_env.options && m_env.options->get("AllHealthBars") == "yes"); // RW 0x6E6179: strcmp with "yes"
		box->winEnable(true);
	}
}

void AptOptionsScreen::writeOptions()
{
	std::string error;
	if (m_env.optionsFile.empty() || !m_env.options->save(m_env.optionsFile, &error))
	{
		windows().note("options-save-failed", m_env.optionsFile.empty() ? std::string("Options.ini: no file name") : error);
	}
}

// ---- lane OPTS-3: the OpenBFME page (NOT RETAIL; see AptSimpleScreens.h and GUI/AptScreens/OpenBFMEOptionsPage.h) -------------------------------------


AptCharacterInst *AptOptionsScreen::find(const std::string &path) const
{
	if (level() < 0)
	{
		return nullptr;
	}
	Apt &apt = const_cast<AptOptionsScreen *>(this)->windows().apt();
	AptSpriteInst *root = apt.level(level());
	return root ? apt.resolvePath(root, path) : nullptr;
}

AptTextInst *AptOptionsScreen::firstText(AptCharacterInst *under, const std::string &name)
{
	std::vector<AptCharacterInst *> stack{ under };
	while (!stack.empty())
	{
		AptCharacterInst *inst = stack.back();
		stack.pop_back();
		if (!inst)
		{
			continue;
		}
		if (inst->type() == AptCharacterInst::Type::EditText && (name.empty() || inst->instName() == name))
		{
			return static_cast<AptTextInst *>(inst);
		}
		if (AptSpriteInst *s = inst->asSprite())
		{
			const auto &children = s->children();
			for (auto it = children.rbegin(); it != children.rend(); ++it)
			{
				stack.push_back(*it);
			}
		}
	}
	return nullptr;
}

void AptOptionsScreen::setFieldText(AptTextInst *field, const std::string &text)
{
	// a field bound to a variable shows the variable (AptRenderList: the parent timeline's member); else its own text
	if (field->text != text)
	{
		field->text = text;
		field->textAssigned = true;
	}
	if (!field->variable.empty() && field->variable.find_first_of("./") == std::string::npos && field->parent())
	{
		AptValue v;
		if (!field->parent()->getMember(field->variable, v) || v.toString() != text)
		{
			field->parent()->setMember(field->variable, AptValue::string(text));
		}
	}
}

std::vector<AptTextInst *> AptOptionsScreen::subtitleTexts() const
{
	std::vector<AptTextInst *> out;
	std::vector<AptCharacterInst *> stack{ find(kSubtitlePath) };
	while (!stack.empty())
	{
		AptCharacterInst *inst = stack.back();
		stack.pop_back();
		if (!inst)
		{
			continue;
		}
		if (inst->type() == AptCharacterInst::Type::EditText)
		{
			out.push_back(static_cast<AptTextInst *>(inst));
		}
		if (AptSpriteInst *sp = inst->asSprite())
		{
			const auto &children = sp->children();
			for (auto it = children.rbegin(); it != children.rend(); ++it)
			{
				stack.push_back(*it);
			}
		}
	}
	return out;
}

AptCharacterInst *AptOptionsScreen::entryClip() const
{
	return find(std::string("Main.FrameLeft.") + kEntryName);
}

bool AptOptionsScreen::entryBox(float out[4]) const
{
	AptCharacterInst *entry = entryClip();
	return entry && m_state == 1 && m_extensionAvailable && entry->globallyVisible() && WindowManager::stageBounds(*entry, out);
}

void AptOptionsScreen::updateEntry()
{
	// the OPENBFME button on the basic page: made once the movie's CUSTOM SETTINGS button exists, placed, captioned every frame (its text field is
	// the copied button's; a frame of its own timeline must not bring the copied caption back)
	if (!m_extensionAvailable || m_state != 1 || level() < 0 || menuMode())
	{
		return; // lane OPTS-5: the menu has no OPENBFME button
	}
	AptCharacterInst *entry = entryClip();
	if (!entry)
	{
		AptCharacterInst *source = find(kEntrySourcePath);
		AptCharacterInst *frame = find("Main.FrameLeft");
		AptSpriteInst *frameSprite = frame ? frame->asSprite() : nullptr;
		if (!source || !frameSprite)
		{
			windows().note("options-openbfme-entry", std::string("[S-3403] Options: no ") + kEntrySourcePath + " to copy the OPENBFME button from");
			return;
		}
		if (!windows().apt().duplicateClip(frameSprite, AptValue::object(source), AptValue::string(kEntryName), AptValue::integer(kEntryScriptDepth), &entry) || !entry)
		{
			windows().note("options-openbfme-entry", "[S-3403] Options: the OPENBFME button could not be made (duplicateMovieClip of " + std::string(kEntrySourcePath) + ")");
			return;
		}
		// the stage point kEntryCentre in FrameLeft's space: the copy keeps the source's scale, only its place moves
		bool ok = false;
		const AptMatrix inverse = frameSprite->globalMatrix().inverse(&ok);
		if (ok)
		{
			float bounds[4];
			float cx = 0, cy = 0;
			if (WindowManager::stageBounds(*source, bounds))
			{
				// the source's art centre relative to its origin, kept for the copy
				float ox, oy;
				source->globalMatrix().apply(0, 0, ox, oy);
				cx = (bounds[0] + bounds[2]) * 0.5f - ox;
				cy = (bounds[1] + bounds[3]) * 0.5f - oy;
			}
			float lx, ly;
			inverse.apply(kEntryCentreX - cx, kEntryCentreY - cy, lx, ly);
			entry->matrix.tx = lx;
			entry->matrix.ty = ly;
		}
	}
	if (AptTextInst *caption = firstText(entry, std::string()))
	{
		setFieldText(caption, kEntryCaption);
	}
}

void AptOptionsScreen::setExtensionHidden(bool hidden)
{
	for (const char *path : kBasicFrames)
	{
		if (AptCharacterInst *f = find(path))
		{
			f->visible = !hidden;
		}
	}
	if (AptCharacterInst *root = level() >= 0 ? windows().apt().level(level()) : nullptr)
	{
		for (AptCharacterInst *c : root->asSprite()->children())
		{
			if (c->instName().compare(0, std::strlen(kTooltipPrefix), kTooltipPrefix) == 0)
			{
				c->visible = !hidden;
			}
		}
	}
	const std::vector<AptTextInst *> subtitles = subtitleTexts();
	if (!hidden && subtitles.size() == m_savedSubtitles.size())
	{
		for (std::size_t i = 0; i < subtitles.size(); ++i)
		{
			// the movie's own text (a "$label" the text records resolve) and the variable the field shows
			const SavedField &f = m_savedSubtitles[i];
			subtitles[i]->text = f.text;
			subtitles[i]->textAssigned = f.assigned;
			if (AptCharacterInst *holder = f.hasVariable ? subtitles[i]->parent() : nullptr)
			{
				holder->setMember(subtitles[i]->variable, f.variableValue);
			}
		}
	}
}

bool AptOptionsScreen::openExtension(std::string *error)
{
	if (m_state == kExtensionState)
	{
		return true;
	}
	AptGadgetLayer *layer = windows().gadgetLayer();
	std::string missing;
	for (const char *path : kBasicFrames)
	{
		if (!find(path))
		{
			missing += std::string(missing.empty() ? "" : ", ") + path;
		}
	}
	if (m_state != 1 || !layer || !ownerWindow() || !missing.empty())
	{
		if (error)
		{
			*error = m_state != 1 ? "the OpenBFME page opens from Options' basic page (the page is " + std::to_string(m_state) + ")"
				: !missing.empty() ? "[S-3403] Options.apt has no " + missing + " (the OpenBFME page takes their place)"
								   : std::string("no gadget layer");
		}
		return false;
	}
	if (!m_page)
	{
		m_page = std::make_unique<OpenBFMEOptionsPage>(*layer, ownerWindow());
		m_page->setSink([this](const OpenBFMEPageEvent &e) {
			// lane OPTS-5: the original game's rows are this screen's own (their retail handlers); the rest goes to the device in one FIFO with the
			// bottom bar's events
			if (e.key.rfind(kRetailKeyPrefix, 0) == 0)
			{
				std::string error;
				if (e.kind == OpenBFMEPageEvent::Kind::Release)
				{
					sliderClicked(); // RW 0x91F90D's 0x4010
				}
				else if (e.kind == OpenBFMEPageEvent::Kind::Value && !setRetailValue(e.key, e.value, &error))
				{
					windows().note("options-menu", "Options: " + error);
				}
				return;
			}
			m_extensionEvents.push_back(e);
		});
	}
	layer->setDecorator([this](GadgetDrawList &out) {
		if (m_page && m_state == kExtensionState)
		{
			m_page->decorate(out);
		}
	});
	m_savedSubtitles.clear();
	for (AptTextInst *subtitle : subtitleTexts())
	{
		SavedField f;
		f.text = subtitle->text;
		f.assigned = subtitle->textAssigned;
		if (!subtitle->variable.empty() && subtitle->variable.find_first_of("./") == std::string::npos && subtitle->parent())
		{
			f.hasVariable = subtitle->parent()->getMember(subtitle->variable, f.variableValue);
		}
		m_savedSubtitles.push_back(f);
	}
	// the mirrored retail keys as Options.ini holds them now: closeExtension refreshes a retail gadget only when the page changed its key
	m_mirroredAtOpen.clear();
	for (const char *key : kMirroredKeys)
	{
		m_mirroredAtOpen.push_back(m_env.options && m_env.options->has(key) ? m_env.options->get(key) : std::string("\x01(none)"));
	}
	m_state = kExtensionState;
	m_entryPressed = false;
	m_barPressed = -1;
	setExtensionHidden(true);
	return true;
}

void AptOptionsScreen::closeExtension()
{
	if (m_state != kExtensionState)
	{
		return;
	}
	m_state = 1;
	if (m_page)
	{
		m_page->hide();
	}
	if (AptGadgetLayer *layer = windows().gadgetLayer())
	{
		layer->setDecorator(nullptr);
	}
	setExtensionHidden(false);
	// the retail gadgets the page mirrors (Options.ini ScrollFactor, AllHealthBars, AlternateMouseSetup) take the file's value only when the page
	// wrote a new one (its ACCEPT): the basic page's own pending edits (a box ticked, not yet saved) stay otherwise, CANCEL included (Sol review r1)
	for (std::size_t i = 0; i < m_mirroredAtOpen.size() && i < std::size(kMirroredKeys); ++i)
	{
		const char *key = kMirroredKeys[i];
		const std::string now = m_env.options && m_env.options->has(key) ? m_env.options->get(key) : std::string("\x01(none)");
		if (now == m_mirroredAtOpen[i])
		{
			continue;
		}
		if (std::strcmp(key, "ScrollFactor") == 0)
		{
			if (GameWindow *sc = gadget("ScrollSpeed"))
			{
				GadgetSliderSetPosition(sc, initialScrollSpeed());
			}
		}
		else if (std::strcmp(key, "AllHealthBars") == 0)
		{
			refreshHealthBarsBox(gadget("HealthBars"));
		}
		else if (GameWindow *mouse = gadget("AlternateMouseSetUp"))
		{
			GadgetCheckBoxSetChecked(mouse, now == "yes");
		}
	}
	m_mirroredAtOpen.clear();
}

std::vector<OpenBFMEPageEvent> AptOptionsScreen::takeExtensionEvents()
{
	// one FIFO: the page's changes and the bottom bar's / the button's clicks in the order they happened (Sol review r1)
	std::vector<OpenBFMEPageEvent> out;
	out.swap(m_extensionEvents);
	return out;
}

bool AptOptionsScreen::mouseFilter(int kind, float x, float y)
{
	if (m_state == kExtensionState)
	{
		if (m_page && m_page->mouse(kind, x, y))
		{
			return true;
		}
		// the bottom bar: Options.apt plays its closing animation before it sends AptOptions::Save / Cancel (the movie's buttons; see
		// test_fb7_menus.cpp clickOptionsButton), so while the page is up a click on CANCEL / RESET TO DEFAULTS / ACCEPT is the page's and never
		// reaches the movie (its hover animation still plays: the moves go on)
		if (kind == 0)
		{
			return false;
		}
		int hit = -1;
		for (int i = 0; i < 3 && hit < 0; ++i)
		{
			// the button's own hit area (the clip's bounds hold the hidden frames of its animation)
			AptCharacterInst *b = find(std::string(kBarButtons[i]) + ".bttn");
			AptButtonInst *button = b ? b->asButton() : nullptr;
			if (button && button->globallyVisible() && button->hitTest(x, y))
			{
				hit = i;
			}
		}
		if (kind == 1)
		{
			m_barPressed = hit;
			return hit >= 0;
		}
		const int pressed = m_barPressed;
		m_barPressed = -1;
		if (pressed < 0)
		{
			return false;
		}
		if (pressed == hit)
		{
			static const OpenBFMEPageEvent::Kind kinds[3] = { OpenBFMEPageEvent::Kind::Cancel, OpenBFMEPageEvent::Kind::Reset, OpenBFMEPageEvent::Kind::Accept };
			m_extensionEvents.push_back({ kinds[pressed], std::string(), 0 });
		}
		return true;
	}
	if (kind == 0)
	{
		return false;
	}
	float box[4];
	const bool inside = entryBox(box) && x >= box[0] && x < box[2] && y >= box[1] && y < box[3];
	if (kind == 1)
	{
		m_entryPressed = inside;
		return inside; // the copied button's own press action (EnterAdvancedSettings) must not run
	}
	if (m_entryPressed)
	{
		m_entryPressed = false;
		if (inside)
		{
			m_extensionEvents.push_back({ OpenBFMEPageEvent::Kind::Entry, std::string(), 0 });
		}
		return true;
	}
	return false;
}

// ---- lane OPTS-5: one Options menu (NOT RETAIL LAYOUT; see AptSimpleScreens.h kOpenBFMEMenu) ------------------------------------------------------

namespace
{
// OpenBFME's reasons for a greyed row of the original game's (NOT RETAIL TEXT; appended to the row's own help text)
const char *const kReasonNotDrawn = "OpenBFME does not draw this setting yet.";
const char *const kReasonNoEax = "OpenBFME has no EAX3 sound.";
const char *const kReasonNoAudioLod = "OpenBFME does not use this setting yet.";
const char *const kReasonHealthBars = "This detail level already shows every health bar.";
const char *const kReasonOnline = "Can be changed from the Options of the online screens.";
const char *const kReasonRefreshNat = "Refresh NAT is not available in OpenBFME yet.";
const char *const kUnavailable = "Unavailable";

// a text of the string file on one line (the movie's tooltips break theirs with "\n")
std::u16string oneLine(std::u16string text)
{
	std::u16string out;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		char16_t c = text[i];
		if (c == u'\\' && i + 1 < text.size() && text[i + 1] == u'n')
		{
			c = u'\n';
			++i;
		}
		if (c == u'\n' || c == u'\r' || c == u'\t')
		{
			c = u' ';
		}
		if (c == u' ' && (out.empty() || out.back() == u' '))
		{
			continue;
		}
		out += c;
	}
	while (!out.empty() && out.back() == u' ')
	{
		out.pop_back();
	}
	return out;
}

// the row's own text, then a sentence of OpenBFME's (the string file's tooltips often end without a full stop)
std::u16string withReason(const std::u16string &help, const std::u16string &reason)
{
	if (help.empty())
	{
		return reason;
	}
	const char16_t last = help.back();
	return help + (last == u'.' || last == u'!' || last == u'?' ? u" " : u". ") + reason;
}

std::u16string withReason(const std::u16string &help, const char *reason)
{
	return withReason(help, asciiToU16(reason));
}
} // namespace

bool AptOptionsScreen::menuMode() const
{
	return kOpenBFMEMenu && m_env.openbfmeOptionsMenu;
}

const std::vector<AptOptionsScreen::RetailRow> &AptOptionsScreen::retailRowTable()
{
	// key, the gadget InitGadgets fills, the name (the movie's own label of the control), the help (the string file's tooltip of the control: the
	// TOOLTIP:Options/tooltip<x> family of the BFME2 movie's tooltip areas, which RotWK's lotr.str still carries, or the label RotWK's own
	// AptButtonTooltipMap.ini maps the control to (Settings/EAX, Settings/HighQualityAudio, RefreshNAT), or the movie's own help text (PortNumHelp,
	// WarnHighGraphicDetail on its custom settings page)). INFERENCE: the pairing of a row with a tooltip whose control RotWK's movie no longer has.
	static const std::vector<RetailRow> rows = {
		{ "Retail.Brightness", "Brightness", "APT:Brightness", "TOOLTIP:Options/tooltipBrightness" },
		{ "Retail.Detail", nullptr, "APT:Graphics", "TOOLTIP:Options/tooltipDetail" }, // + 0x310: the screen's level, not the movie's list (opened over a game the list is not made)
		{ "Retail.AdvancedOption0", nullptr, "APT:AdvancedOption_ModelLOD", "APT:WarnHighGraphicDetail" },
		{ "Retail.AdvancedOption1", nullptr, "APT:AdvancedOption_AnimationLOD", "TOOLTIP:Options/tooltipShowAnimations" },
		{ "Retail.AdvancedOption2", nullptr, "APT:AdvancedOption_EffectsLOD", "TOOLTIP:Options/tooltipParticleCap" },
		{ "Retail.AdvancedOption3", nullptr, "APT:AdvancedOption_ShadowLOD", "TOOLTIP:Options/tooltip3DShadows" },
		{ "Retail.AdvancedOption4", nullptr, "APT:AdvancedOption_TerrainLOD", "TOOLTIP:Options/tooltipTerrainLighting" },
		{ "Retail.AdvancedOption5", nullptr, "APT:AdvancedOption_WaterLOD", "TOOLTIP:Options/tooltipSmoothWaterBorder" },
		{ "Retail.AdvancedOption6", nullptr, "APT:AdvancedOption_TextureQualityLOD", "TOOLTIP:Options/tooltipTextureDetail" },
		{ "Retail.AdvancedOption7", nullptr, "APT:AdvancedOption_ShaderLOD", "APT:WarnHighGraphicDetail" },
		{ "Retail.AdvancedOption8", nullptr, "APT:AdvancedOption_DecalLOD", "TOOLTIP:Options/tooltipShowUnitDecals" },
		{ "Retail.MusicVolume", "MusicVolume", "APT:Music", "TOOLTIP:Options/tooltipMusic" },
		{ "Retail.SoundFxVolume", "SoundFxVolume", "APT:SoundFx", "TOOLTIP:Options/tooltipSoundFX" },
		{ "Retail.VoiceVolume", "VoiceVolume", "APT:Voice", "TOOLTIP:Options/tooltipVoice" },
		{ "Retail.AmbientVolume", "AmbientVolume", "APT:Ambient", "TOOLTIP:Options/tooltipAmbient" },
		{ "Retail.MovieVolume", "MovieVolume", "APT:Movie", "TOOLTIP:Options/tooltipMovie" },
		{ "Retail.EAX3", "EAX3", "APT:EAX3", "ToolTip:Settings/EAX" },
		{ "Retail.HighAudioQuality", "HighAudioQuality", "APT:HighAudioQuality", "ToolTip:Settings/HighQualityAudio" },
		{ "Retail.ScrollSpeed", "ScrollSpeed", "APT:ScrollSpeed", "TOOLTIP:Options/tooltipScrollSpeed" },
		{ "Retail.HealthBars", "HealthBars", "APT:EnableHealthBars", "TOOLTIP:Options/tooltipEnableHealthBars" },
		{ "Retail.OnlineIp", "OnlineIp", "APT:OnlineIP", "TOOLTIP:OnlineIP" },
		{ "Retail.OnlinePortNum", "OnlinePortNum", "APT:PortNum", "APT:PortNumHelp" },
		{ "Retail.SendDelay", "SendDelay", "APT:SendDelay", "TOOLTIP:SendDelay" },
		{ "Retail.RefreshNat", nullptr, "APT:RefreshNat", "TOOLTIP:RefreshNat" },
	};
	return rows;
}

std::array<int, GameLODManager::kAdvancedOptionCount> AptOptionsScreen::shownAdvancedSettings() const
{
	// the custom settings (+ 0x314, a text of nine numbers) when the level is Custom, else the level's preset (RW 0x601BBD)
	std::array<int, GameLODManager::kAdvancedOptionCount> out{};
	if (m_masterOption == GameLODManager::kCustomLevel)
	{
		OptionPreferences scratch;
		AdvancedOptionPrefs::parseInto(m_customTemplate, scratch);
		for (int i = 0; i < GameLODManager::kAdvancedOptionCount; ++i)
		{
			out[(std::size_t)i] = AdvancedOptionPrefs::getSetting(scratch, i);
		}
		return out;
	}
	if (m_masterOption >= 0 && m_masterOption < GameLODManager::kCustomLevel)
	{
		out = m_env.gameLOD ? m_env.gameLOD->presetSettings(m_masterOption) : GameLODManager().presetSettings(m_masterOption);
	}
	return out;
}

std::vector<OpenBFMEPageRow> AptOptionsScreen::retailRows() const
{
	std::vector<OpenBFMEPageRow> out;
	const std::array<int, GameLODManager::kAdvancedOptionCount> advanced = shownAdvancedSettings();
	for (const RetailRow &info : retailRowTable())
	{
		OpenBFMEPageRow row;
		row.key = info.key;
		row.label = oneLine(fetchOrMissing(m_env.gameText, info.nameLabel));
		row.help = oneLine(fetchOrMissing(m_env.gameText, info.helpLabel));
		const std::string name = info.gadget ? info.gadget : "";
		GameWindow *w = info.gadget ? gadget(name) : nullptr;
		if (info.gadget && !w)
		{
			continue; // the movie made no such control (menuUpdate reported it)
		}
		if (row.key == "Retail.Detail")
		{
			row.kind = OpenBFMEPageRow::Kind::Choice;
			for (const auto &item : kDetailItems)
			{
				row.items.push_back({ oneLine(fetchOrMissing(m_env.gameText, item.first)), true });
				if (item.second == m_masterOption)
				{
					row.value = (int)row.items.size() - 1;
				}
			}
			row.enabled = m_allowAdvanced;
			if (!row.enabled)
			{
				row.help = withReason(row.help, oneLine(fetchOrMissing(m_env.gameText, "APT:CannotChangeGraphics"))); // the movie's own text for it
			}
		}
		else if (!info.gadget && row.key.rfind("Retail.AdvancedOption", 0) == 0)
		{
			const int index = row.key.back() - '0';
			const AdvancedOptionInfo &o = GameLODManager::advancedOption(index);
			row.kind = OpenBFMEPageRow::Kind::Choice;
			for (int j = 0; j < o.count; ++j)
			{
				std::string label = std::string("APT:AdvancedOption_") + o.key + "_" + o.choices[j];
				if (label == "APT:AdvancedOption_ShadowLOD_UltraHigh")
				{
					label = "APT:AdvancedOption_ShaderLOD_UltraHigh"; // RW 0x921515 .. 0x92152E
				}
				row.items.push_back({ oneLine(fetchOrMissing(m_env.gameText, label)), true });
			}
			row.value = advanced[(std::size_t)index];
			row.enabled = m_allowAdvanced && GameLODManager::advancedOptionDrawn(index);
			if (!m_allowAdvanced)
			{
				row.help = withReason(row.help, oneLine(fetchOrMissing(m_env.gameText, "APT:CannotChangeGraphics")));
			}
			else if (!row.enabled)
			{
				row.help = withReason(row.help, kReasonNotDrawn); // stop S-3402
			}
		}
		else if (row.key == "Retail.RefreshNat")
		{
			row.kind = OpenBFMEPageRow::Kind::Text; // RotWK's button (AptOptions::RefreshNat): not ported, stop S-1913
			row.valueText = asciiToU16(kUnavailable);
			row.enabled = false;
			row.help = withReason(row.help, kReasonRefreshNat);
		}
		else if (BitTest(w->winGetStyle(), GWS_HORZ_SLIDER))
		{
			row.kind = OpenBFMEPageRow::Kind::Slider;
			GadgetSliderGetMinMax(w, &row.minValue, &row.maxValue);
			row.value = GadgetSliderGetPosition(w);
			row.valueText = asciiToU16(std::to_string(row.value));
			row.enabled = w->winGetEnabled();
		}
		else if (BitTest(w->winGetStyle(), GWS_CHECK_BOX))
		{
			row.kind = OpenBFMEPageRow::Kind::Check;
			row.value = GadgetCheckBoxIsChecked(w) ? 1 : 0;
			row.enabled = w->winGetEnabled();
			if (!row.enabled && name == "HealthBars")
			{
				row.help = withReason(row.help, kReasonHealthBars); // RW 0x91EB77 / 0x91EBD1: Very Low, or Custom with Decal Detail Off, checks and disables it
			}
			else if (!row.enabled && name == "EAX3")
			{
				row.help = withReason(row.help, kReasonNoEax); // stop S-1913
			}
			else if (!row.enabled && name == "HighAudioQuality")
			{
				row.help = withReason(row.help, kReasonNoAudioLod); // stop S-2481
			}
			else if (!row.enabled && name == "SendDelay")
			{
				row.help = withReason(row.help, kReasonOnline); // NetworkEnabled 0 (RW 0x91ED91's first argument)
			}
		}
		else if (BitTest(w->winGetStyle(), GWS_COMBO_BOX))
		{
			row.kind = OpenBFMEPageRow::Kind::Choice;
			// OnlineIp: InitGadgets listed the machine's addresses in this order (RW 0x719C53)
			for (const auto &a : m_env.localAddresses)
			{
				row.items.push_back({ asciiToU16(a.first), true });
			}
			int selected = -1;
			GadgetComboBoxGetSelectedPos(w, &selected);
			row.value = selected;
			row.enabled = w->winGetEnabled();
			if (!row.enabled)
			{
				row.help = withReason(row.help, kReasonOnline);
			}
		}
		else if (BitTest(w->winGetStyle(), GWS_ENTRY_FIELD))
		{
			row.kind = OpenBFMEPageRow::Kind::Entry;
			row.valueText = GadgetTextEntryGetText(w);
			row.maxValue = 5; // RW 0x81606D(entry, 5)
			int number = 0;
			for (char16_t c : row.valueText)
			{
				if (c < u'0' || c > u'9')
				{
					break;
				}
				number = number * 10 + (c - u'0');
			}
			row.value = number;
			row.enabled = w->winGetEnabled();
			if (!row.enabled)
			{
				row.help = withReason(row.help, kReasonOnline);
			}
		}
		else
		{
			continue;
		}
		out.push_back(std::move(row));
	}
	return out;
}

void AptOptionsScreen::refreshRetailRows()
{
	if (!m_page || !m_page->shown())
	{
		return;
	}
	for (const OpenBFMEPageRow &row : retailRows())
	{
		m_page->setRow(row); // false: the tab shown has no such row
	}
}

bool AptOptionsScreen::setRetailValue(const std::string &key, int value, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = why;
		}
		return false;
	};
	const RetailRow *info = nullptr;
	for (const RetailRow &r : retailRowTable())
	{
		if (key == r.key)
		{
			info = &r;
		}
	}
	if (!info)
	{
		return fail("no setting '" + key + "'");
	}
	const std::vector<OpenBFMEPageRow> rows = retailRows();
	const OpenBFMEPageRow *row = nullptr;
	for (const OpenBFMEPageRow &r : rows)
	{
		if (r.key == key)
		{
			row = &r;
		}
	}
	if (!row)
	{
		return fail("the setting '" + key + "' has no control (the movie made none)");
	}
	if (!row->enabled)
	{
		return fail("the setting '" + key + "' is disabled");
	}
	if ((row->kind == OpenBFMEPageRow::Kind::Choice && (value < 0 || value >= (int)row->items.size())) ||
		(row->kind == OpenBFMEPageRow::Kind::Slider && (value < row->minValue || value > row->maxValue)) || (row->kind == OpenBFMEPageRow::Kind::Check && value != 0 && value != 1) ||
		(row->kind == OpenBFMEPageRow::Kind::Entry && (value < 0 || value > 99999)))
	{
		return fail("the setting '" + key + "' does not take " + std::to_string(value));
	}
	const std::string name = info->gadget ? info->gadget : "";
	if (key == "Retail.Detail")
	{
		// the Detail list (RW 0x91F90D's 0x4026, lane PLAY-2): Custom opens the custom settings with the level's settings (EnterAdvancedSettings RW
		// 0x91F043 takes the level shown before: + 0x314 = that preset's); another level becomes + 0x310 and the health bars box follows (RW 0x91EBD1)
		const int level = kDetailItems[value].second;
		if (level == GameLODManager::kCustomLevel)
		{
			if (m_masterOption != GameLODManager::kCustomLevel)
			{
				m_customTemplate = AdvancedOptionPrefs::format(shownAdvancedSettings());
			}
			m_masterOption = level;
			refreshHealthBarsBox(gadget("HealthBars"));
		}
		else
		{
			m_masterOption = level;
			refreshHealthBarsBox(gadget("HealthBars"));
		}
		selectDetail();
	}
	else if (!info->gadget && key.rfind("Retail.AdvancedOption", 0) == 0)
	{
		// a custom setting changed (the movie's custom settings page writes MasterOption0TemplateCustom and makes the level Custom): + 0x314 is the
		// settings shown with this one changed
		std::array<int, GameLODManager::kAdvancedOptionCount> settings = shownAdvancedSettings();
		settings[(std::size_t)(key.back() - '0')] = value;
		m_customTemplate = AdvancedOptionPrefs::format(settings);
		m_masterOption = GameLODManager::kCustomLevel;
		selectDetail();
		refreshHealthBarsBox(gadget("HealthBars")); // RW 0x920595 after the custom page's DONE
	}
	else
	{
		GameWindow *w = gadget(name);
		switch (row->kind)
		{
			case OpenBFMEPageRow::Kind::Slider:
				// the gadget moves as a drag moves it, and the screen hears the track message (RW 0x91F90D: the volume or the gamma at once)
				GadgetSliderSetPosition(w, value);
				gadgetMessage(w, GSM_SLIDER_TRACK, reinterpret_cast<WindowMsgData>(w), (WindowMsgData)value);
				break;
			case OpenBFMEPageRow::Kind::Check:
				if ((value != 0) != GadgetCheckBoxIsChecked(w))
				{
					GadgetCheckBoxToggle(w); // the click's toggle (GBM_SELECTED to the screen)
				}
				break;
			case OpenBFMEPageRow::Kind::Choice:
				GadgetComboBoxSetSelectedPos(w, value);
				break;
			case OpenBFMEPageRow::Kind::Entry:
				GadgetTextEntrySetText(w, value > 0 ? asciiToU16(std::to_string(value)) : std::u16string());
				break;
			default:
				return fail("the setting '" + key + "' takes no value");
		}
	}
	refreshRetailRows();
	return true;
}

void AptOptionsScreen::menuUpdate()
{
	if (!menuMode() || m_menuOpened || level() < 0)
	{
		return;
	}
	// until the page is up the basic page's frames stay hidden (the menu replaces them; its opening animation shows none of them)
	setExtensionHidden(true);
	if (m_state != 1)
	{
		return; // AptOptions::OnInitialized has not run: the gadgets are not filled yet
	}
	std::string error;
	if (!openExtension(&error))
	{
		windows().note("options-menu", "[S-4670] Options: the OpenBFME menu cannot open: " + error);
		m_menuOpened = true; // reported once; the screen shows nothing of the retail page either (the frames stay hidden)
		return;
	}
	m_menuOpened = true;
	// the custom settings start from the level's (EnterAdvancedSettings RW 0x91F043); Custom keeps the saved ones (presetTemplate)
	m_customTemplate = presetTemplate(m_masterOption);
	std::string missing;
	for (const RetailRow &info : retailRowTable())
	{
		if (info.gadget && !gadget(info.gadget))
		{
			missing += std::string(missing.empty() ? "" : ", ") + info.gadget;
		}
	}
	if (!missing.empty())
	{
		windows().note("options-menu", "[S-4670] Options: the movie made no " + missing + ": the menu has no row for it");
	}
	m_extensionEvents.push_back({ OpenBFMEPageEvent::Kind::Entry, std::string(), m_openOnDetail ? 1 : 0 });
}

void AptOptionsScreen::finishMenu(bool accept)
{
	if (!menuMode() || m_state != kExtensionState)
	{
		windows().note("options-menu", "Options: finishMenu outside the OpenBFME menu");
		return;
	}
	if (m_page)
	{
		m_page->hide();
	}
	if (AptGadgetLayer *layer = windows().gadgetLayer())
	{
		layer->setDecorator(nullptr);
	}
	if (accept)
	{
		save(); // RW 0x91FC9C: the settings written and applied, the screen closes
	}
	else
	{
		cancel(); // RW 0x91EC52: the saved gamma and volumes back, the screen closes
	}
}

void AptOptionsScreen::resetMenu()
{
	reset(); // RW 0x91F4D1
	// the custom settings page's reset (MasterOption0ResetDefault, the LOD manager's ideal level + 0x17C4): the level the game takes without a
	// StaticGameLOD, IdealStaticGameLOD when Options.ini has one, else the hardware detection's (not ported, S-2481: GameLODManager::kDetectedLevel)
	int ideal = GameLODManager::kDetectedLevel;
	if (m_env.options && m_env.options->has("IdealStaticGameLOD"))
	{
		const int level = GameLODManager::findLevel(m_env.options->get("IdealStaticGameLOD"));
		if (level >= 0 && level < GameLODManager::kCustomLevel)
		{
			ideal = level;
		}
	}
	if (m_allowAdvanced)
	{
		m_masterOption = ideal;
		m_customTemplate = presetTemplate(ideal);
		selectDetail();
		refreshHealthBarsBox(gadget("HealthBars"));
	}
	refreshRetailRows();
}
