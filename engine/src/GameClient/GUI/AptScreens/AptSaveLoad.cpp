// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptSaveLoad.h (lane MP-2).

#include "GameClient/GUI/AptScreens/AptSaveLoad.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/SaveLoadInfo.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"

#include <cstdio>
#include <set>

namespace
{
// RW 0x818D96 .. 0x818DAE: the columns' share of the list's width, in percent
const int kColumnPercent[4] = { 0x1C, 0x24, 0x13, 0x11 };
const Color kCompatible = 0xFFFFFFFFu;   // RW 0x81916F
const Color kIncompatible = 0xFF808080u; // RW 0x819166

UnicodeString toU16(const std::string &utf8)
{
	// the names are UTF-8; the lists hold UCS-2 (code points above U+FFFF do not occur in file and map names)
	UnicodeString out;
	for (size_t i = 0; i < utf8.size();)
	{
		const unsigned char c = (unsigned char)utf8[i];
		unsigned cp = c;
		size_t n = 1;
		if (c >= 0xE0 && i + 2 < utf8.size())
		{
			cp = ((c & 0x0Fu) << 12) | (((unsigned char)utf8[i + 1] & 0x3Fu) << 6) | ((unsigned char)utf8[i + 2] & 0x3Fu);
			n = 3;
		}
		else if (c >= 0xC0 && i + 1 < utf8.size())
		{
			cp = ((c & 0x1Fu) << 6) | ((unsigned char)utf8[i + 1] & 0x3Fu);
			n = 2;
		}
		out.push_back((char16_t)cp);
		i += n;
	}
	return out;
}
} // namespace

AptSaveLoad::AptSaveLoad(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "SaveLoad.apt", "AptSaveLoad"), m_env(environment)
{
	registerCommand("AptSaveLoad::OnInitialized", [this](const std::string &) { populate(); });
	registerCommand("AptSaveLoad::OnClosed", [](const std::string &) {});
	registerCommand("AptSaveLoad::Load", [this](const std::string &) { load(); });
	registerCommand("AptSaveLoad::Save", unportedCommand("AptSaveLoad::Save"));
	registerCommand("AptSaveLoad::Delete", unportedCommand("AptSaveLoad::Delete"));
	registerCommand("AptSaveLoad::Cancel", [this](const std::string &) {
		this->windows().requestShellPop(); // RW 0x816146: the screen closes (the shell pops it on the next update)
	});
	registerCommand("AptSaveLoad::ConfirmationOk", unportedCommand("AptSaveLoad::ConfirmationOk"));
	registerCommand("AptSaveLoad::ConfirmationCancel", [](const std::string &) {});
	registerScreenRef("AptSaveLoad::InitGadgets", [this](const std::string &instanceName, GameWindow *w) {
		// RW 0x817667 compares the name with "GameList", "AutoSaveList", "FileNameTextEntry"; SaveLoad.apt names its placeholders "2~GameList" ..., the part
		// after '~' is compared (INFERENCE: the prefix is the movie's tab order, which retail's gadget names do not carry, S-1126)
		if (!w)
		{
			return;
		}
		const size_t tilde = instanceName.rfind('~');
		const std::string name = tilde == std::string::npos ? instanceName : instanceName.substr(tilde + 1);
		if (name == "GameList")
		{
			m_gameList = w;
		}
		else if (name == "AutoSaveList")
		{
			m_autoSaveList = w;
		}
		else if (name == "FileNameTextEntry")
		{
			m_fileEntry = w;
		}
		else
		{
			this->windows().note("unknown-gadget", "AptSaveLoad::InitGadgets(" + name + ")");
		}
		populate();
	});
	// RW 0x816164 (the provider names of RW 0xDA726C)
	registerProvider("SaveLoadMode", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			return true;
		}
		value = "0";
		const SaveLoadInfo *info = m_env.saveLoad;
		if (info && info->mode == 3)
		{
			value = "Save";
		}
		else if (info && info->mode == 2)
		{
			value = "Load";
		}
		return true;
	});
	registerProvider("GameTypes", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			return true;
		}
		const unsigned flags = m_env.saveLoad ? m_env.saveLoad->flags : 0u;
		value.clear();
		const char *names[5] = { "Campaign", "Skirmish", "Replay", "WOTRSP", "WOTRMP" };
		for (int i = 0; i < 5; ++i)
		{
			if (flags & (1u << i))
			{
				value += names[i];
			}
		}
		return true;
	});
	registerProvider("CurrentGameType", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "0";
			return true;
		}
		const char *names[5] = { "Campaign", "Skirmish", "Replay", "WOTRSP", "WOTRMP" };
		for (int i = 0; i < 5; ++i)
		{
			if (value == names[i])
			{
				m_gameType = 1 << i;
			}
		}
		return true;
	});
	if (!m_env.saveLoad)
	{
		this->windows().note("provider-unwired", "SaveLoad.apt: the host gave no SaveLoadInfo [S-1126]");
	}
	else if (m_env.saveLoad->flags & 7u & ~4u)
	{
		this->windows().note("unported-command", "SaveLoad.apt: only the replay page is ported, not the saved games [S-1126]");
	}
	const unsigned flags = m_env.saveLoad ? m_env.saveLoad->flags : 4u;
	m_gameType = (flags & 1u) ? 1 : (flags & 2u) ? 2 : (flags & 4u) ? 4 : m_gameType; // RW 0x816692
}

void AptSaveLoad::runInit()
{
	AptScreen::runInit();
	populate();
}

void AptSaveLoad::prune()
{
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (!layer)
	{
		return;
	}
	const std::vector<GameWindow *> live = layer->gadgets().allWindows();
	const std::set<const GameWindow *> alive(live.begin(), live.end());
	for (GameWindow **w : { &m_gameList, &m_autoSaveList, &m_fileEntry, &m_selectedList })
	{
		if (*w && !alive.count(*w))
		{
			*w = nullptr;
		}
	}
}

void AptSaveLoad::populate()
{
	// RW 0x818D41 (the replay page): both lists, once they exist
	prune();
	if (m_populated || !m_gameList || !m_autoSaveList || !m_env.saveLoad)
	{
		return;
	}
	m_populated = true;
	for (GameWindow *list : { m_gameList, m_autoSaveList })
	{
		GadgetListBoxReset(list);
		if (GadgetListBoxGetNumColumns(list) != 4)
		{
			GadgetListBoxSetColumnWidths(list, 4, kColumnPercent); // RW 0x818DC9 (RW 0x726D36: the column count and their percentages)
		}
	}
	for (const SaveLoadInfo::Replay &r : m_env.saveLoad->replays)
	{
		const bool last = r.fileNameUtf8 == m_env.saveLoad->lastReplayNameUtf8;
		GameWindow *list = last ? m_autoSaveList : m_gameList;
		const Color c = r.compatible ? kCompatible : kIncompatible;
		const int row = GadgetListBoxAddEntryText(list, toU16(r.mapUtf8), c, -1, 0);
		GadgetListBoxAddEntryText(list, toU16(r.fileNameUtf8), c, row, 1);
		GadgetListBoxAddEntryText(list, toU16(r.timeUtf8), c, row, 2); // the movie's column headers: Map, Name, Time, Date
		GadgetListBoxAddEntryText(list, toU16(r.dateUtf8), c, row, 3);
		(last ? m_autoSavePaths : m_gameListPaths).push_back(r.path);
		char colour[16];
		std::snprintf(colour, sizeof(colour), "%08X", c);
		m_rows.push_back(std::string(last ? "AutoSaveList" : "GameList") + "|" + r.mapUtf8 + "|" + r.fileNameUtf8 + "|" + r.timeUtf8 + "|" + r.dateUtf8 + "|" + colour);
	}
}

WindowMsgHandledType AptSaveLoad::gadgetMessage(GameWindow *, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	prune();
	GameWindow *gadget = reinterpret_cast<GameWindow *>(data1);
	if ((msg == GLM_SELECTED || msg == GLM_DOUBLE_CLICKED) && gadget && (gadget == m_gameList || gadget == m_autoSaveList))
	{
		// the selection of one list is the file (RW 0x8162E5 reads the list that holds one)
		m_selectedList = gadget;
		m_selectedRow = (int)(std::intptr_t)data2;
		if (gadget == m_gameList && m_autoSaveList && m_selectedRow >= 0)
		{
			GadgetListBoxSetSelected(m_autoSaveList, -1);
		}
		else if (gadget == m_autoSaveList && m_gameList && m_selectedRow >= 0)
		{
			GadgetListBoxSetSelected(m_gameList, -1);
		}
		if (msg == GLM_DOUBLE_CLICKED)
		{
			load();
		}
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

void AptSaveLoad::load()
{
	// RW 0x816DBF: the Replay type loads the selected file at once (RW 0x816981)
	prune();
	if (m_gameType != 4)
	{
		windows().note("unported-command", "AptSaveLoad::Load of a saved game [S-1126]");
		return;
	}
	// RW 0x8162E5: the GameList's selection, else the AutoSaveList's
	const std::vector<std::string> *paths = nullptr;
	int row = -1;
	if (m_gameList)
	{
		GadgetListBoxGetSelected(m_gameList, &row);
		paths = &m_gameListPaths;
	}
	if (row < 0 && m_autoSaveList)
	{
		GadgetListBoxGetSelected(m_autoSaveList, &row);
		paths = &m_autoSavePaths;
	}
	if (!paths || row < 0 || row >= (int)paths->size())
	{
		windows().note("command-ignored", "AptSaveLoad::Load: no replay is selected");
		return;
	}
	m_selectedRow = row;
	m_loaded = (*paths)[(size_t)m_selectedRow];
	if (m_env.services)
	{
		m_env.services->request(ShellRequest{ ShellAction::LoadReplayFile, m_loaded });
	}
	else
	{
		windows().note("command-unwired", "AptSaveLoad::Load: the host gave no shell services [S-1126]");
	}
}
