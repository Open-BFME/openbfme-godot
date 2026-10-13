// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptSaveLoad.h (lane MP-2; lane SAVE-2: the saved-game pages).

#include "GameClient/GUI/AptScreens/AptSaveLoad.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/SaveLoadInfo.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"

#include <cstdio>
#include <set>

namespace
{
// RW 0x818D96 .. 0x818DAE (and populateSaveGameListbox's { 28, 36, 19, 17 }): the columns' share of the list's width, in percent
const int kColumnPercent[4] = { 0x1C, 0x24, 0x13, 0x11 };
const Color kCompatible = 0xFFFFFFFFu;   // RW 0x81916F
const Color kIncompatible = 0xFF808080u; // RW 0x819166
const Color kNewSave = 0xFFC8C8C8u;      // populateSaveGameListbox: GameMakeColor(200, 200, 200, 255)
const Color kCampaignSave = 0xFFEFCC52u; // GameMakeColor(239, 204, 82, 255): a campaign save (types 1 and 4)
const char *const kTypeNames[5] = { "Campaign", "Skirmish", "Replay", "WOTRSP", "WOTRMP" };

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

std::u16string trimmed(const std::u16string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == u' ' || s[a] == u'\t'))
	{
		++a;
	}
	while (b > a && (s[b - 1] == u' ' || s[b - 1] == u'\t'))
	{
		--b;
	}
	return s.substr(a, b - a);
}

// the same file name ignoring case (the Save\ folder is a Windows folder in retail; a name differing in case is the same file there)
bool sameName(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i)
	{
		char x = a[i], y = b[i];
		x = (x >= 'A' && x <= 'Z') ? (char)(x - 'A' + 'a') : x;
		y = (y >= 'A' && y <= 'Z') ? (char)(y - 'A' + 'a') : y;
		if (x != y)
		{
			return false;
		}
	}
	return true;
}
} // namespace

AptSaveLoad::AptSaveLoad(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "SaveLoad.apt", "AptSaveLoad"), m_env(environment)
{
	windows.setBackground(1); // lane FB7-1: the constructor shows the front-end background (RW 0x818BDC .. 0x818BDE)
	registerCommand("AptSaveLoad::OnInitialized", [this](const std::string &) {
		m_state = 0; // RW 0x816157 (the decomp's 0x00433DF1: state 1, which the update turns into the lists enabled and state 0)
		populate();
	});
	registerCommand("AptSaveLoad::OnClosed", [](const std::string &) {});
	registerCommand("AptSaveLoad::Load", [this](const std::string &) { load(); });
	registerCommand("AptSaveLoad::Save", [this](const std::string &) { save(); });
	registerCommand("AptSaveLoad::Delete", [this](const std::string &) { remove(); });
	registerCommand("AptSaveLoad::Cancel", [this](const std::string &) {
		if (m_state == 0)
		{
			this->windows().requestShellPop(); // RW 0x816146: the screen closes (the shell pops it on the next update)
		}
	});
	registerCommand("AptSaveLoad::ConfirmationOk", [this](const std::string &) { confirmationOk(); });
	registerCommand("AptSaveLoad::ConfirmationCancel", [this](const std::string &) {
		// bound to the same body as OnInitialized (the decomp's 0x00433DF1): the confirmation is dropped, the lists are usable again
		m_pendingPath.clear();
		m_pendingName.clear();
		m_state = 0;
		refreshButtons(); // state 1: RW 0x816927
	});
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
			// RW 0x817667: emptied, at most 40 characters (validation flags 8: not settable here, S-1409; Save refuses file-name characters itself)
			GadgetTextEntrySetText(w, UnicodeString());
			if (EntryData *entry = static_cast<EntryData *>(w->winGetUserData()))
			{
				entry->maxTextLen = 40;
			}
			setDefaultFileName();
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
		for (int i = 0; i < 5; ++i)
		{
			if (flags & (1u << i))
			{
				value += kTypeNames[i];
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
		int type = m_gameType;
		for (int i = 0; i < 5; ++i)
		{
			if (value == kTypeNames[i])
			{
				type = 1 << i;
			}
		}
		if (type != m_gameType)
		{
			// the decomp's Externs query 2: a new type, state 2 (the lists are filled again for it)
			m_gameType = type;
			m_populated = false;
			populate();
		}
		return true;
	});
	if (!m_env.saveLoad)
	{
		this->windows().note("provider-unwired", "SaveLoad.apt: the host gave no SaveLoadInfo [S-1126]");
	}
	else if (m_env.saveLoad->flags & ~7u)
	{
		this->windows().note("unported-command", "SaveLoad.apt: the War of the Ring and multiplayer pages are not ported [S-4003]");
	}
	const unsigned flags = m_env.saveLoad ? m_env.saveLoad->flags : 4u;
	m_gameType = (flags & 1u) ? 1 : (flags & 2u) ? 2 : (flags & 4u) ? 4 : m_gameType; // RW 0x816692
	m_generation = m_env.saveLoad ? m_env.saveLoad->generation : 0;
}

AptSaveLoad::~AptSaveLoad() = default;

void AptSaveLoad::runInit()
{
	AptScreen::runInit();
	populate();
}

void AptSaveLoad::frameUpdate()
{
	if (m_box)
	{
		m_box->update();
	}
	if (m_env.saveLoad && m_env.saveLoad->generation != m_generation)
	{
		// the host changed the lists (a file was deleted): filled again, as the update's state 2 does after RW 0x81707F
		m_generation = m_env.saveLoad->generation;
		m_populated = false;
		populate();
	}
}

std::u16string AptSaveLoad::text(const std::string &label) const
{
	return fetchOrMissing(m_env.gameText, label);
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
	prune();
	// the replay page waits for both lists (RW 0x818D41 fills both); the saved-game list needs the GameList only (populateSaveGameListbox takes a null
	// AutoSaveList: the movie's Load page has none)
	if (m_populated || !m_gameList || (m_gameType == 4 && !m_autoSaveList) || !m_env.saveLoad)
	{
		return;
	}
	m_populated = true;
	m_rows.clear();
	m_gameListPaths.clear();
	m_autoSavePaths.clear();
	m_gameListSaves.clear();
	for (GameWindow *list : { m_gameList, m_autoSaveList })
	{
		if (!list)
		{
			continue;
		}
		GadgetListBoxReset(list);
		if (GadgetListBoxGetNumColumns(list) != 4)
		{
			GadgetListBoxSetColumnWidths(list, 4, kColumnPercent); // RW 0x818DC9 (RW 0x726D36: the column count and their percentages)
		}
	}
	if (m_gameType == 4)
	{
		populateReplays();
	}
	else
	{
		populateSaves();
	}
	refreshButtons(); // the update's state 2: the lists filled (RW 0x6E04AD / 0x818D41), then RW 0x816927
}

void AptSaveLoad::populateReplays()
{
	// RW 0x818D41 (the replay page)
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

void AptSaveLoad::populateSaves()
{
	// GameState::populateSaveGameListbox (RW 0x6E04AD; the decomp's GameStateAutoSave.cpp): the new-save row on the save page, then the files of the type
	auto addRow = [this](const std::u16string &c0, const std::string &c0Utf8, const SaveLoadInfo::Save *s, Color colour, int index) {
		const int row = GadgetListBoxAddEntryText(m_gameList, c0, colour, -1, 0);
		std::string c1, c2, c3;
		if (s)
		{
			c1 = s->retailImport ? s->fileNameUtf8 + " (import: coming later)" : s->fileNameUtf8;
			c2 = s->timeUtf8;
			c3 = s->dateUtf8;
		}
		GadgetListBoxAddEntryText(m_gameList, toU16(c1), colour, row, 1);
		GadgetListBoxAddEntryText(m_gameList, toU16(c2), colour, row, 2);
		GadgetListBoxAddEntryText(m_gameList, toU16(c3), colour, row, 3);
		m_gameListSaves.push_back(index);
		m_gameListPaths.push_back(s ? s->path : std::string());
		char hex[16];
		std::snprintf(hex, sizeof(hex), "%08X", colour);
		m_rows.push_back("GameList|" + c0Utf8 + "|" + c1 + "|" + c2 + "|" + c3 + "|" + hex);
	};
	if (m_env.saveLoad->mode == 3)
	{
		const std::u16string newSave = text("GUI:NewSaveGame");
		addRow(newSave, loadScreenU16ToUtf8(newSave), nullptr, kNewSave, -1);
	}
	const std::vector<SaveLoadInfo::Save> &saves = m_env.saveLoad->saves;
	for (size_t i = 0; i < saves.size(); ++i)
	{
		const SaveLoadInfo::Save &s = saves[i];
		if (s.gameKind != m_gameType)
		{
			continue;
		}
		Color colour = s.gameKind == 1 ? kCampaignSave : kCompatible;
		if (!s.loadable || s.retailImport)
		{
			colour = kIncompatible; // OpenBFME: a save this build cannot load, a retail save (import is SAVE-7)
		}
		const std::string map = mapName(s);
		addRow(toU16(map), map, &s, colour, (int)i);
	}
	// the GameList's first row selected (OpenBFME saves no auto-saves: the AutoSaveList stays empty, S-4003)
	if (GadgetListBoxGetNumEntries(m_gameList) > 0)
	{
		GadgetListBoxSetSelected(m_gameList, 0);
		if (m_autoSaveList)
		{
			GadgetListBoxSetSelected(m_autoSaveList, -1);
		}
		m_selectedList = m_gameList;
		m_selectedRow = 0;
	}
	setDefaultFileName();
}

std::string AptSaveLoad::mapName(const SaveLoadInfo::Save &save) const
{
	// the decomp's getMapDisplayName (GameStateAutoSave.cpp, BFME2 0x002DC16A; RW 0x6E04AD's caller): the map cache's base display name, else the game text
	// of the saved label, else the label itself
	if (m_env.skirmish && !save.mapPathUtf8.empty())
	{
		for (const MapCacheEntry &m : m_env.skirmish->maps())
		{
			if (m.name == save.mapPathUtf8)
			{
				return loadScreenU16ToUtf8(SkirmishGameSetup(*m_env.skirmish, m_env.gameText).mapBaseDisplayName(m));
			}
		}
	}
	const std::string label = !save.mapUtf8.empty() && save.mapUtf8[0] == '$' ? save.mapUtf8.substr(1) : save.mapUtf8;
	std::u16string text;
	if (m_env.gameText && !label.empty() && m_env.gameText->fetch(label, text))
	{
		return loadScreenU16ToUtf8(text);
	}
	return label;
}

int AptSaveLoad::selectedSave()
{
	prune();
	int row = -1;
	if (m_gameList)
	{
		GadgetListBoxGetSelected(m_gameList, &row);
	}
	if (row < 0 || row >= (int)m_gameListSaves.size())
	{
		return -1;
	}
	return m_gameListSaves[(size_t)row];
}

void AptSaveLoad::setDefaultFileName()
{
	// RW 0x816FA3: the selected entry's file name, else (GameState RW 0x6DE38D) GUI:DefaultSaveFileName with the first number from 1 whose file does not exist
	prune();
	if (!m_fileEntry || !m_env.saveLoad || m_env.saveLoad->mode != 3 || m_gameType == 4)
	{
		return;
	}
	const int index = selectedSave();
	if (index >= 0)
	{
		GadgetTextEntrySetText(m_fileEntry, toU16(m_env.saveLoad->saves[(size_t)index].fileNameUtf8));
		return;
	}
	const std::string pattern = loadScreenU16ToUtf8(text("GUI:DefaultSaveFileName"));
	const size_t at = pattern.find("%d");
	if (at == std::string::npos)
	{
		windows().note("save-default-name", "GUI:DefaultSaveFileName has no %d: '" + pattern + "'");
		GadgetTextEntrySetText(m_fileEntry, toU16(pattern));
		return;
	}
	for (int n = 1; n < 100000000; ++n) // RW 0x6DE445: up to 99,999,999
	{
		const std::string name = pattern.substr(0, at) + std::to_string(n) + pattern.substr(at + 2);
		bool exists = false;
		for (const SaveLoadInfo::Save &s : m_env.saveLoad->saves)
		{
			exists = exists || (!s.retailImport && sameName(s.fileNameUtf8, name));
		}
		if (!exists)
		{
			GadgetTextEntrySetText(m_fileEntry, toU16(name));
			return;
		}
	}
	GadgetTextEntrySetText(m_fileEntry, UnicodeString()); // RW 0x6DE450: every number taken
}

void AptSaveLoad::enableLists(bool enable)
{
	// the update's states 14 .. 17 disable the entry and both lists while a confirmation is up (the decomp's 0x00437053); state 1 enables them again
	prune();
	for (GameWindow *w : { m_fileEntry, m_gameList, m_autoSaveList })
	{
		if (w)
		{
			w->winEnable(enable);
		}
	}
}

void AptSaveLoad::askConfirmation(const char *kind, int waitState)
{
	// RW 0x819548: the movie's showMessageBox(kind) (RW 0xC50300), then the state that waits for ConfirmationOk / ConfirmationCancel
	enableLists(false);
	m_confirmations.push_back(kind);
	std::string error;
	if (!windows().invokeAS(level(), "showMessageBox", { kind }, nullptr, &error))
	{
		windows().note("saveload-confirmation", std::string("showMessageBox(") + kind + "): " + error);
	}
	m_state = waitState;
}

void AptSaveLoad::revealButton(const std::string &button)
{
	// OpenBFME (S-4003): a pressed main button stays at the end of its press animation until the movie's own confirmation flow reveals it (its
	// RevealButtonList after a showMessageBox); after OpenBFME's own boxes (a refusal, a save's error) the button is revealed the same way
	std::string error;
	if (!windows().invokeASAt(level(), "MainButtons." + button, "gotoAndPlay", { "_reveal" }, nullptr, &error))
	{
		windows().note("saveload-buttons", "MainButtons." + button + ".gotoAndPlay(_reveal): " + error);
	}
}

void AptSaveLoad::showBox(const std::u16string &title, const std::u16string &body, bool closeAfter, const std::string &pressedButton)
{
	if (!m_box)
	{
		m_box = std::make_unique<AptMessageBox>(windows(), shell());
	}
	m_closeAfterBox = closeAfter;
	m_boxButton = closeAfter ? std::string() : pressedButton;
	m_boxOpen = true;
	enableLists(false);
	m_box->show(AptMessageBox::TYPE_OK, title, body, [this](int) {
		m_boxOpen = false;
		m_state = 0;
		refreshButtons();
		if (m_closeAfterBox)
		{
			windows().requestShellPop(); // retail state 9 after the save: the screen goes
		}
		else if (!m_boxButton.empty())
		{
			revealButton(m_boxButton);
		}
	});
	if (m_box->level() < 0)
	{
		// GuiFX.apt did not load (noted by the box): the screen is never left disabled
		m_boxOpen = false;
		enableLists(true);
		m_state = 0;
	}
}

std::u16string AptSaveLoad::boxTitle() const
{
	return m_box ? m_box->title() : std::u16string();
}

std::u16string AptSaveLoad::boxText() const
{
	return m_box ? m_box->text() : std::u16string();
}

WindowMsgHandledType AptSaveLoad::gadgetMessage(GameWindow *, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	// RW 0x8180A8 (the decomp's 0x00435BB3, tier A): nothing while a confirmation or a result box is up (states 0x12 .. 0x17)
	prune();
	GameWindow *gadget = reinterpret_cast<GameWindow *>(data1);
	if (m_state >= 0x12 && m_state <= 0x17)
	{
		return MSG_IGNORED;
	}
	if (msg == GLM_SELECTED && gadget && (gadget == m_gameList || gadget == m_autoSaveList))
	{
		// the selection of one list is the file (RW 0x8162E5 reads the list that holds one): the other list's goes
		m_selectedList = gadget;
		m_selectedRow = (int)(std::intptr_t)data2;
		if (m_selectedRow >= 0)
		{
			GameWindow *other = gadget == m_gameList ? m_autoSaveList : m_gameList;
			if (other)
			{
				GadgetListBoxSetSelected(other, -1);
			}
		}
		refreshButtons(); // RW 0x816927
		if (m_env.saveLoad && m_env.saveLoad->mode != 2)
		{
			setDefaultFileName(); // RW 0x816FA3: the save page's entry takes the selected file's name
		}
		saveButtonEnable(); // RW 0x816F24
		return MSG_HANDLED;
	}
	if (msg == GLM_DOUBLE_CLICKED && gadget && (gadget == m_gameList || gadget == m_autoSaveList))
	{
		// a double click on a file: Save on the save page (RW 0x817B5F), Load on the load page (RW 0x816DBF)
		if (m_env.saveLoad && m_env.saveLoad->mode == 3)
		{
			save();
		}
		else if (m_env.saveLoad && m_env.saveLoad->mode == 2)
		{
			load();
		}
		return MSG_HANDLED;
	}
	if (msg == GEM_EDIT_DONE && gadget && gadget == m_fileEntry)
	{
		// RW 0x818100 (retail's 0x4031): Enter in the name entry saves, on the save page, with the message's second word 0
		if (data2 == 0 && m_env.saveLoad && m_env.saveLoad->mode == 3)
		{
			save();
		}
		return MSG_HANDLED;
	}
	if (msg == GEM_UPDATE_TEXT && gadget && gadget == m_fileEntry)
	{
		saveButtonEnable(); // RW 0x8180EC (retail's 0x4032): the name text changed, the movie's SaveButtonEnable follows it (RW 0x816F24)
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

void AptSaveLoad::refreshButtons()
{
	// RW 0x816927: the lists and the entry enabled, then RW 0x8165A1 on the load page: the movie's LoadButtonEnable / DeleteButtonEnable, "1" with a
	// selected file ("0" for none; the multiplayer page's other-player case does not arise here)
	enableLists(true);
	if (!m_env.saveLoad || m_env.saveLoad->mode != 2)
	{
		return;
	}
	bool selected = false;
	if (m_gameType == 4)
	{
		int row = -1;
		if (m_gameList)
		{
			GadgetListBoxGetSelected(m_gameList, &row);
		}
		if (row < 0 && m_autoSaveList)
		{
			GadgetListBoxGetSelected(m_autoSaveList, &row);
		}
		selected = row >= 0;
	}
	else
	{
		selected = selectedSave() >= 0;
	}
	for (const char *fn : { "LoadButtonEnable", "DeleteButtonEnable" })
	{
		std::string error;
		m_buttonCalls.push_back(std::string(fn) + (selected ? " 1" : " 0"));
		if (!windows().invokeAS(level(), fn, { selected ? "1" : "0" }, nullptr, &error))
		{
			windows().note("saveload-buttons", std::string(fn) + ": " + error);
		}
	}
}

void AptSaveLoad::saveButtonEnable()
{
	// RW 0x816F24: the movie's SaveButtonEnable, "1" when the trimmed entry text is not empty
	prune();
	if (!m_fileEntry)
	{
		return;
	}
	std::string error;
	const bool hasText = !trimmed(GadgetTextEntryGetText(m_fileEntry)).empty();
	m_buttonCalls.push_back(hasText ? "SaveButtonEnable 1" : "SaveButtonEnable 0");
	if (!windows().invokeAS(level(), "SaveButtonEnable", { hasText ? "1" : "0" }, nullptr, &error))
	{
		windows().note("saveload-buttons", "SaveButtonEnable: " + error);
	}
}

void AptSaveLoad::load()
{
	// RW 0x816DBF: only in state 0
	prune();
	if (m_state != 0)
	{
		windows().note("command-ignored", "AptSaveLoad::Load in state " + std::to_string(m_state));
		return;
	}
	if (m_gameType != 4)
	{
		const int index = selectedSave();
		if (index < 0 || !m_env.saveLoad)
		{
			windows().note("command-ignored", "AptSaveLoad::Load: no saved game is selected");
			return;
		}
		const SaveLoadInfo::Save &s = m_env.saveLoad->saves[(size_t)index];
		if (s.retailImport || !s.loadable)
		{
			// OpenBFME: the refusal is shown, nothing loads (retail's own error box is GUI:ErrorLoadingGame, RW 0x6DF55E)
			loadRefused(s.fileNameUtf8, s.retailImport ? "This is a saved game of The Rise of the Witch-king itself. Importing retail saves is coming later."
														: s.refusalUtf8);
			return;
		}
		m_pendingPath = s.path;
		if (m_env.saveLoad->inGame)
		{
			askConfirmation("Load", 18); // state 14 -> 18: "Current game data will be lost. Load saved file?"
			return;
		}
		doLoad(); // state 3
		return;
	}
	// RW 0x8162E5: the GameList's selection, else the AutoSaveList's; the Replay type loads it at once (RW 0x816981)
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

void AptSaveLoad::loadRefused(const std::string &nameUtf8, const std::string &errorUtf8)
{
	std::u16string title = text("GUI:ErrorLoadingGame"); // "Error loading game '%s'" (RW 0x6DF55E's message box)
	const size_t at = title.find(u"%s");
	if (at != std::u16string::npos)
	{
		title = title.substr(0, at) + toU16(nameUtf8) + title.substr(at + 2);
	}
	windows().note("save-refused", nameUtf8 + ": " + errorUtf8 + " [S-4003]");
	// the box's text field keeps one line per line: the sentences and clauses go on lines of their own (a long line is shrunk to the box's width)
	std::string lines;
	for (size_t i = 0; i < errorUtf8.size(); ++i)
	{
		lines.push_back(errorUtf8[i]);
		if ((errorUtf8[i] == ';' || errorUtf8[i] == '.') && i + 1 < errorUtf8.size() && errorUtf8[i + 1] == ' ')
		{
			lines.push_back('\n');
			++i;
		}
	}
	showBox(title, toU16(lines), false, "Load");
}

void AptSaveLoad::doLoad()
{
	// state 3: the screen closes (closeDelayed "OnClosed") and the file loads; the host loads it and leaves the shell for the game
	m_loaded = m_pendingPath;
	m_pendingPath.clear();
	m_state = 0;
	if (m_env.services)
	{
		m_env.services->request(ShellRequest{ ShellAction::LoadGameFile, m_loaded });
	}
	else
	{
		windows().note("command-unwired", "AptSaveLoad::Load: the host gave no shell services [S-4003]");
	}
}

void AptSaveLoad::save()
{
	// RW 0x817B5F
	prune();
	if (m_state != 0 || !m_env.saveLoad || m_env.saveLoad->mode != 3 || m_gameType == 4)
	{
		windows().note("command-ignored", "AptSaveLoad::Save: not the save page in state 0 (state " + std::to_string(m_state) + ")");
		return;
	}
	const std::u16string name = m_fileEntry ? trimmed(GadgetTextEntryGetText(m_fileEntry)) : std::u16string();
	if (name.empty())
	{
		windows().note("command-ignored", "AptSaveLoad::Save: no file name"); // the movie's SaveButtonEnable keeps Save off without a name
		return;
	}
	for (char16_t c : name)
	{
		if (c < 0x20 || c == u'/' || c == u'\\' || c == u':' || c == u'*' || c == u'?' || c == u'"' || c == u'<' || c == u'>' || c == u'|')
		{
			// retail's validation flag 8 (RW 0x75E4DF) refuses these while typing (S-1409: not settable here): Save refuses them, named
			showBox(text("APT:SaveGameProgress"), u"A saved game's name cannot contain \\ / : * ? \" < > |", false, "Save");
			return;
		}
	}
	m_pendingName = loadScreenU16ToUtf8(name);
	for (const SaveLoadInfo::Save &s : m_env.saveLoad->saves)
	{
		if (!s.retailImport && sameName(s.fileNameUtf8, m_pendingName))
		{
			askConfirmation("Save", 19); // state 15 -> 19: "Overwrite existing file?"
			return;
		}
	}
	doSave();
}

void AptSaveLoad::doSave()
{
	// RW 0x817719 -> state 4 -> RW 0x817D0E: the host writes the file and answers saveFinished
	const std::string name = m_pendingName;
	m_pendingName.clear();
	m_state = 4;
	enableLists(false);
	if (m_env.services)
	{
		m_env.services->request(ShellRequest{ ShellAction::SaveGameFile, name });
	}
	else
	{
		windows().note("command-unwired", "AptSaveLoad::Save: the host gave no shell services [S-4003]");
		saveFinished(false, "the host gave no shell services");
	}
}

void AptSaveLoad::saveFinished(bool ok, const std::string &errorUtf8)
{
	// RW 0x817D0E: APT:SaveGameProgress with GUI:GameSaved; OpenBFME: a failure shows its error in the same box (retail: APT:SaveFileDiskFull before)
	m_state = 9;
	showBox(text("APT:SaveGameProgress"), ok ? text("GUI:GameSaved") : toU16(errorUtf8), ok, "Save");
}

void AptSaveLoad::remove()
{
	// RW 0x81680D: a selection -> state 16 -> showMessageBox("Delete") -> state 20
	prune();
	if (m_state != 0 || m_gameType == 4)
	{
		windows().note("command-ignored", m_gameType == 4 ? "AptSaveLoad::Delete of a replay [S-1126]" : "AptSaveLoad::Delete in state " + std::to_string(m_state));
		return;
	}
	const int index = selectedSave();
	if (index < 0 || !m_env.saveLoad)
	{
		windows().note("command-ignored", "AptSaveLoad::Delete: no saved game is selected");
		return;
	}
	m_pendingPath = m_env.saveLoad->saves[(size_t)index].path;
	askConfirmation("Delete", 20);
}

void AptSaveLoad::doDelete()
{
	// RW 0x81707F: the file goes, the lists are filled again (the host deletes it and raises SaveLoadInfo::generation)
	const std::string path = m_pendingPath;
	m_pendingPath.clear();
	enableLists(true);
	m_state = 0;
	if (m_env.services)
	{
		m_env.services->request(ShellRequest{ ShellAction::DeleteGameFile, path });
	}
	else
	{
		windows().note("command-unwired", "AptSaveLoad::Delete: the host gave no shell services [S-4003]");
	}
}

void AptSaveLoad::confirmationOk()
{
	// RW 0x817CC4 (the waiting states of the update's showMessageBox calls: 18 load, 19 save, 20 delete)
	if (m_state == 18)
	{
		enableLists(true);
		doLoad();
	}
	else if (m_state == 19)
	{
		doSave();
	}
	else if (m_state == 20)
	{
		doDelete();
	}
	else
	{
		windows().note("command-ignored", "AptSaveLoad::ConfirmationOk in state " + std::to_string(m_state));
	}
}
