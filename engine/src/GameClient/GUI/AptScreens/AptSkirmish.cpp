// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptSkirmish.h.

#include "GameClient/GUI/AptScreens/AptSkirmish.h"

#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"

#include "GameClient/GUI/AptGadgetLayer.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <cctype>
#include <set>

namespace
{
std::string asciiLower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::string u16ToUtf8(const std::u16string &s)
{
	std::string out;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		std::uint32_t c = s[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
		{
			c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
			++i;
		}
		if (c < 0x80)
		{
			out.push_back((char)c);
		}
		else if (c < 0x800)
		{
			out.push_back((char)(0xC0 | (c >> 6)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
		else if (c < 0x10000)
		{
			out.push_back((char)(0xE0 | (c >> 12)));
			out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
		else
		{
			out.push_back((char)(0xF0 | (c >> 18)));
			out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
			out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
	}
	return out;
}

std::u16string trimU16(const std::u16string &s)
{
	std::size_t a = 0, b = s.size();
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

const char *leafName(int leaf)
{
	static const char *names[] = { "Player", "PlayerTemplate", "Team", "Color", "Handicap", "Hero" };
	return names[leaf];
}

void *itemData(int value)
{
	return reinterpret_cast<void *>(static_cast<std::intptr_t>(value));
}
int itemValue(void *data)
{
	return static_cast<int>(reinterpret_cast<std::intptr_t>(data));
}

// Selects the entry whose item data is `value` (BFME1 SkirmishScreenState::refreshGameSlot: the loop over GadgetComboBoxGetItemData; no match:
// entry 0).  `dontHide` is true so the open list is not closed by the refresh.
void selectByItemData(GameWindow *combo, int value)
{
	const int n = GadgetComboBoxGetLength(combo);
	int index = 0;
	for (int i = 0; i < n; ++i)
	{
		if (itemValue(GadgetComboBoxGetItemData(combo, i)) == value)
		{
			index = i;
			break;
		}
	}
	if (n > 0)
	{
		GadgetComboBoxSetSelectedPos(combo, index, true);
	}
}

// The colour column is an ImageComboBox (GadgetImageComboBox.cpp), not a ZH combo box.
void selectImageByItemData(GameWindow *combo, int value)
{
	const int n = GadgetImageComboBoxGetLength(combo);
	int index = 0;
	for (int i = 0; i < n; ++i)
	{
		if (itemValue(GadgetImageComboBoxGetItemData(combo, i)) == value)
		{
			index = i;
			break;
		}
	}
	if (n > 0)
	{
		GadgetImageComboBoxSetSelectedPos(combo, index);
	}
}

void fillImageCombo(GameWindow *combo, const std::vector<SkirmishComboEntry> &entries)
{
	GadgetImageComboBoxReset(combo);
	for (const SkirmishComboEntry &e : entries)
	{
		const int at = GadgetImageComboBoxAddEntry(combo, e.image, e.color);
		GadgetImageComboBoxSetItemData(combo, at, itemData(e.itemData));
	}
}

void fillCombo(GameWindow *combo, const std::vector<SkirmishComboEntry> &entries)
{
	GadgetComboBoxReset(combo);
	for (const SkirmishComboEntry &e : entries)
	{
		const int at = GadgetComboBoxAddEntry(combo, e.text, e.color);
		GadgetComboBoxSetItemData(combo, at, itemData(e.itemData));
	}
}
} // namespace

const std::vector<std::string> &AptSkirmish::retailNames()
{
	static const std::vector<std::string> names = {
		"AptSkirmish::InitGadgets", "AptSkirmish::SuppressStrategicPromo", "AptSkirmish::OnProfilePopupCancel",
		"AptSkirmish::OnAddProfileAccept", "AptSkirmish::OnExitStatsScreen", "AptSkirmish::OnChangeProfile",
		"AptSkirmish::OnChangeProfileMenu", "AptSkirmish::OnDeleteProfileMenu", "AptSkirmish::OnNewProfileMenu",
		"AptSkirmish::OnStatsMenu", "AptSkirmish::OnDeleteProfile", "AptSkirmish::StartGame", "AptSkirmish::Back",
		"AptSkirmish::Exit", "AptSkirmish::OnClosed", "AptSkirmish::OnInitialized"
	};
	return names;
}

const std::vector<std::string> &AptSkirmish::lobbyNames()
{
	// binary order (0x00853F94.. and 0x00884F4C..): MpGameSetup, AptMpGameRules, AptMapPreview, AptMpClans, AptMpChat
	static const std::vector<std::string> names = {
		"MpGameSetup::InitGadgets", "AptMpGameRules::ShowChat", "AptMpGameRules::ShowClans", "MpGameSetup::IsInitialized", "MpGameSetup::HostMode",
		"MpGameSetup::OnTabSelect", "MpGameSetup::OnReadyPress", "MpGameSetup::OnKickPlayer", "MpGameSetup::OnSortPlayers", "MpGameSetup::OnSortName",
		"MpGameSetup::OnSortIcons", "AptMapPreview::GameMapType", "AptMapPreview::MapGadgetInit", "AptMapPreview::Picture", "AptMpGameRules::InitGadgets",
		"AptMpGameRules::Reset", "MpGameRules::NumComboBoxes", "MpGameRules::NumCheckBoxes", "AptMpClans::InitGadgets", "AptMpClans::WebSite",
		"AptMpClans::Delete", "AptMpChat::InitGadgets", "AptMpChat::Send"
	};
	return names;
}

AptSkirmish::AptSkirmish(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptSkirmish(windows, shell, environment, "Skirmish.apt", "AptSkirmish", false)
{
}

AptSkirmish::~AptSkirmish()
{
	// lane UI-2: AptMapPreview's teardown (RW 0x9769E8) releases the picture the render callback draws
	windows().clearRenderPicture("AptMapPreview::Picture");
}

AptSkirmish::AptSkirmish(WindowManager &windows, Shell &shell, ShellEnvironment &environment, const char *filename, const char *codePrefix, bool lan)
	: AptScreen(windows, shell, filename, codePrefix), m_env(environment), m_lan(lan)
{
	m_profiles = environment.profiles;
	if (!m_profiles)
	{
		m_ownProfiles = std::make_unique<MemorySkirmishProfiles>();
		m_profiles = m_ownProfiles.get();
		windows.note("skirmish-environment-missing", "SkirmishProfileStore: the profiles are kept in memory only [S-179]");
	}
	if (!environment.skirmish || !environment.newGame)
	{
		windows.note("skirmish-environment-missing", std::string(!environment.skirmish ? "SkirmishSetupSource" : "NewGameSink") + ": the lobby has no model and StartGame cannot post a game");
	}
	else
	{
		if (!environment.gameText)
		{
			windows.note("skirmish-environment-missing", "GameTextSource: every label reads as MISSING: '<label>'");
		}
		m_setup = std::make_unique<SkirmishGameSetup>(*environment.skirmish, environment.gameText);
		if (!m_setup->init(playerName(), environment.newGame->newGameSeed(), std::string(), &m_error))
		{
			windows.note("skirmish-setup-failed", m_error);
			m_setup.reset();
		}
	}
	// lane UI-2: the map window's inferences (GadgetMapPreview.cpp, AptSkirmish::updateCurrentMapWindow)
	windows.note("map-preview-inferences", "[S-1480] map window: the preview file is read where it is mounted (retail copies it to MapPreviews first), its image name is the path with '\\' and ':' as '_', start spots are shown in the skirmish lobby only (TheGameLogic + 0x114 == 3 taken as the screen kind) and enabled, an APT gadget's parent is its owner, the start-spot click is forwarded but not acted on");
	if (m_lan)
	{
		registerLobby(); // lane MP-2: AptLanLobby registers its own commands and update
		return;
	}
	registerAll();
	if (m_setup)
	{
		windows.note("skirmish-inferences", "[S-178] lobby contents inferred: faction rule, AI on every map, no honors stars, Hero / Handicap empty, no rule rows, ImageComboBox behaviour, `_global.InGame` set by the manager");
	}
	windows.note("skirmish-profile-unverified", "[S-179] profile-first, profile storage and StartGame without a profile are not verified against retail");
}

std::u16string AptSkirmish::playerName() const
{
	const std::u16string current = m_profiles->current();
	if (!current.empty())
	{
		return current;
	}
	// InitSkirmishGameGadgets: the name field starts as TheGameText->fetch("GUI:Player")
	return fetchOrMissing(m_env.gameText, "GUI:Player");
}

void AptSkirmish::registerAll()
{
	// commands: Back, StartGame, the profile commands and the notified ones; InitGadgets is the screen reference (below)
	registerCommand("AptSkirmish::Back", [this](const std::string &) { windows().requestShellPop(); });
	registerCommand("AptSkirmish::StartGame", [this](const std::string &) {
		// RotWK 0x9280F0: only the state is set; the next screen update validates and posts the game
		m_profileState = kStateStart;
	});
	registerCommand("AptSkirmish::Exit", [this](const std::string &) {
		// BFME1 SkirmishScreenExit.cpp (0x005791C0): the exit animation is finished and the published game info released; nothing is shown
		windows().note("unported-command", "AptSkirmish::Exit: animation / game-info release only [S-175]");
	});
	registerCommand("AptSkirmish::OnInitialized", [this](const std::string &) {
		++m_initializedCount;
		m_profileState = kStateInitialized; // RotWK 0x9280DB; the update does the rest [S-179]
	});
	// the screen's update: owned by this screen, removed with it (AptScreen::~AptScreen)
	windows().addUpdateListener(this, [this]() { update(); });
	registerProfileCommands();
	registerScreenRef("AptSkirmish::InitGadgets", [this](const std::string &name, GameWindow *w) { initSkirmishGadget(name, w); });
	registerLobby();
}

void AptSkirmish::registerProfileCommands()
{
	// the commands below prune first (see pruneDeadGadgets)
	registerCommand("AptSkirmish::SuppressStrategicPromo", [this](const std::string &) {
		m_suppressStrategicPromo = true;
		windows().note("unported-command", "AptSkirmish::SuppressStrategicPromo: remembered for this screen only, no preference is written [S-179]");
	});
	registerCommand("AptSkirmish::OnStatsMenu", [this](const std::string &) { windows().note("unported-command", "AptSkirmish::OnStatsMenu: the stats screen (SkirmishBattleHonors) is not ported [S-175]"); });
	registerCommand("AptSkirmish::OnExitStatsScreen", [this](const std::string &) { windows().note("unported-command", "AptSkirmish::OnExitStatsScreen [S-175]"); });
	registerCommand("AptSkirmish::OnNewProfileMenu", [this](const std::string &) {
		++m_newProfileMenuCount;
		pruneDeadGadgets();
		m_profileState = kStateNewProfile;
		// BFME1 SkirmishScreenOnInitGadget.cpp: the persona entry starts empty and takes the focus; a new popup page starts with an empty name, so its accept
		// button is disabled (PopupSelectBttnDisable, RotWK 0x929063 region; the flag at +0x6C2 = 1)
		if (m_profileEntry)
		{
			GadgetTextEntrySetText(m_profileEntry, std::u16string());
			m_profileEntry->manager().winSetFocus(m_profileEntry);
		}
		// without the entry (the popup page is still opening) the entry's own InitGadgets clears it when it appears
		invokeRoot("PopupSelectBttnDisable");
		m_acceptDisabled = true;
	});
	registerCommand("AptSkirmish::OnProfilePopupCancel", [this](const std::string &) {
		pruneDeadGadgets();
		if (m_profileEntry)
		{
			GadgetTextEntrySetText(m_profileEntry, std::u16string());
		}
		closePopupState();
	});
	registerCommand("AptSkirmish::OnAddProfileAccept", [this](const std::string &) {
		pruneDeadGadgets();
		if (addProfileFromEntry())
		{
			m_profileState = kStateClosed;
		}
	});
	registerCommand("AptSkirmish::OnChangeProfileMenu", [this](const std::string &) {
		m_profileState = kStateChangeProfile;
		refreshProfileList();
	});
	registerCommand("AptSkirmish::OnDeleteProfileMenu", [this](const std::string &) {
		m_profileState = kStateDeleteProfile;
		refreshProfileList();
	});
	registerCommand("AptSkirmish::OnClosed", [this](const std::string &) { refreshProfileList(); });
	registerCommand("AptSkirmish::OnChangeProfile", [this](const std::string &) {
		pruneDeadGadgets();
		if (changeProfileFromList())
		{
			m_profileState = kStateClosed;
		}
	});
	registerCommand("AptSkirmish::OnDeleteProfile", [this](const std::string &) {
		pruneDeadGadgets();
		deleteProfileFromList();
	});
}

void AptSkirmish::invokeRoot(const char *function)
{
	std::string err;
	if (level() < 0 || !windows().invokeAS(level(), function, {}, nullptr, &err))
	{
		windows().note("movie-function-missing", std::string("_root.") + function + ": " + err);
	}
}

bool AptSkirmish::addProfileFromEntry()
{
	if (!m_profileEntry)
	{
		windows().note("profile-gadget-missing", "Skirmish::CreatePersonaEntry has not been initialised");
		return false;
	}
	// the handler trims and rejects an empty or duplicate name on its own: the button's enabled state is only "the raw text is not empty"
	const std::u16string name = trimU16(GadgetTextEntryGetText(m_profileEntry));
	if (!m_profiles->add(name))
	{
		windows().note("profile-rejected", name.empty() ? "an empty profile name" : "a profile of that name exists: " + u16ToUtf8(name));
		return false;
	}
	m_profiles->setCurrent(name);
	GadgetTextEntrySetText(m_profileEntry, std::u16string());
	applyProfileName();
	refreshProfileList();
	return true;
}

bool AptSkirmish::changeProfileFromList()
{
	if (!m_profileList)
	{
		windows().note("profile-gadget-missing", "Skirmish::SelectProfile has not been initialised");
		return false;
	}
	int selected = -1;
	GadgetListBoxGetSelected(m_profileList, &selected);
	if (selected < 0)
	{
		windows().note("profile-rejected", "a profile change with no profile selected");
		return false;
	}
	m_profiles->setCurrent(GadgetListBoxGetText(m_profileList, selected, 0));
	applyProfileName();
	refreshProfileList();
	return true;
}

bool AptSkirmish::deleteProfileFromList()
{
	if (!m_profileList)
	{
		windows().note("profile-gadget-missing", "Skirmish::SelectProfile has not been initialised");
		return false;
	}
	int selected = -1;
	GadgetListBoxGetSelected(m_profileList, &selected);
	if (selected < 0)
	{
		windows().note("profile-rejected", "a profile delete with no profile selected");
		return false;
	}
	m_profiles->remove(GadgetListBoxGetText(m_profileList, selected, 0));
	applyProfileName();
	refreshProfileList();
	return true;
}

void AptSkirmish::acceptAction()
{
	// RotWK 0x9294B3: CloseProfilePopup first, then the state decides
	pruneDeadGadgets();
	const int state = m_profileState;
	if (state != kStateNewProfile && state != kStateChangeProfile)
	{
		return;
	}
	invokeRoot("CloseProfilePopup");
	const bool done = state == kStateNewProfile ? addProfileFromEntry() : changeProfileFromList();
	if (done)
	{
		m_profileState = kStateClosed;
	}
}

void AptSkirmish::closePopupState()
{
	// RotWK 0x92810A: the new-profile (2) and change-profile (4) pages close to state 7 and the delete page (3) to 5; closing the new-profile page while
	// there is no profile at all cleans up and leaves the screen (0x927FDA: the lobby is not playable without a profile) [S-179]
	switch (m_profileState)
	{
		case kStateNewProfile:
			m_profileState = kStateClosed;
			if (m_profiles->current().empty())
			{
				windows().requestShellPop();
			}
			break;
		case kStateChangeProfile:
			m_profileState = kStateClosed;
			break;
		case kStateDeleteProfile:
			m_profileState = kStateMain;
			break;
		default:
			break;
	}
}

void AptSkirmish::update()
{
	// RotWK 0x928CDB
	switch (m_profileState)
	{
		case kStateInitialized:
			// ShowMain, state 5; with no current profile ShowAddProfile opens the add-profile popup (which sends OnNewProfileMenu)
			invokeRoot("ShowMain");
			m_profileState = kStateMain;
			if (m_profiles->current().empty())
			{
				invokeRoot("ShowAddProfile");
			}
			break;
		case kStateStart:
			// RotWK 0x928D1A: MpGameSetup validation (0x8431B8): ok -> the movie's CloseMain and state 11; refused -> the movie's ButtonReset (the start
			// button works again) and state 7.  The game is posted here, not when the close animation ends [S-179].
			if (startGame())
			{
				invokeRoot("CloseMain");
				m_profileState = kStateStarting;
			}
			else
			{
				invokeRoot("ButtonReset");
				m_profileState = kStateClosed;
			}
			break;
		default:
			break;
	}
}

void AptSkirmish::registerLobby()
{
	// ---- screen references (`_Init` of the placeholders) ----
	registerScreenRef("MpGameSetup::InitGadgets", [this](const std::string &name, GameWindow *w) { initMpGameSetupGadget(name, w); });
	registerScreenRef("AptMapPreview::MapGadgetInit", [this](const std::string &name, GameWindow *w) { initMapPreviewGadget(name, w); });
	// lane UI-1: AptMpChat::InitGadgets (RW 0x987E4B): "Chat", "ChatPlayers", "ChatEntry"
	registerScreenRef("AptMpChat::InitGadgets", [this](const std::string &name, GameWindow *w) { initChatGadget(name, w); });
	for (const char *ref : { "AptMpGameRules::InitGadgets", "AptMpClans::InitGadgets" })
	{
		const std::string refName = ref;
		registerScreenRef(refName, [this, refName](const std::string &name, GameWindow *) {
			windows().note("unported-gadget-init", refName + "(" + name + ") [S-178]");
		});
	}
	// ---- providers ----
	registerProvider("AptMapPreview::GameMapType", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			setGameMapType(value);
			return true;
		}
		value = m_gameMapType;
		return true;
	});
	registerProvider("MpGameSetup::HostMode", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			m_hostModeText = value;
			m_hostMode = value == "1";
			return true;
		}
		value = m_hostModeText;
		return true;
	});
	registerProvider("MpGameSetup::IsInitialized", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			m_isInitializedText = value;
			return true;
		}
		value = m_isInitializedText;
		return true;
	});
	// the chat and clan tabs belong to online play; the generated rule rows have no data source in the engine yet [S-178]
	// lane UI-1: AptMpGameRules::ShowChat is RW 0x845AE2's id 0: the host interface's slot 4 (the LAN lobby: TheLAN's slot 0xD8, RW 0x846D9A) as "1" / "0"
	registerProvider("AptMpGameRules::ShowChat", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = showChat() ? "1" : "0";
		}
		return true;
	});
	for (const char *name : { "AptMpGameRules::ShowClans", "MpGameRules::NumCheckBoxes", "MpGameRules::NumComboBoxes" })
	{
		registerProvider(name, [](const std::string &, std::string &value, bool setting) {
			if (!setting)
			{
				value = "0";
			}
			return true;
		});
	}
	// ---- commands ----
	registerCommand("MpGameSetup::OnTabSelect", [this](const std::string &argument) { m_activeTab = argument; });
	registerCommand("MpGameSetup::OnReadyPress", [this](const std::string &argument) { onReadyPress(argument); });
	registerCommand("MpGameSetup::OnKickPlayer", [this](const std::string &argument) { onKickPlayer(argument); });
	registerCommand("AptMpChat::Send", [this](const std::string &) { sendChat(); }); // RW 0x987E3C: the argument is not read
	// RW 0x845BCC .. 0x845C80: the three sort commands call RW 0x84010E with 4 / 0 / 2
	registerCommand("MpGameSetup::OnSortIcons", [this](const std::string &) { setMapSort(4); });
	registerCommand("MpGameSetup::OnSortName", [this](const std::string &) { setMapSort(0); });
	registerCommand("MpGameSetup::OnSortPlayers", [this](const std::string &) { setMapSort(2); });
	for (const char *name : { "AptMpGameRules::Reset", "AptMpClans::WebSite", "AptMpClans::Delete" })
	{
		registerCommand(name, unportedCommand(name));
	}
	// ---- the map picture render component (a device texture) ----
	registerComponent("AptMapPreview::Picture", [this](AptComponentRequest &) -> std::shared_ptr<GameWindow> {
		windows().note("unported-component", "AptMapPreview::Picture: the map picture is a device texture [S-177]");
		return nullptr;
	});
}

void AptSkirmish::onReadyPress(const std::string &)
{
	windows().note("unported-command", "MpGameSetup::OnReadyPress");
}

void AptSkirmish::onKickPlayer(const std::string &)
{
	windows().note("unported-command", "MpGameSetup::OnKickPlayer");
}

void AptSkirmish::onChatSend(const std::u16string &)
{
	windows().note("unported-command", "AptMpChat::Send: the skirmish lobby has no one to send to");
}

void AptSkirmish::initChatGadget(const std::string &name, GameWindow *window)
{
	// AptMpChat::InitGadgets RW 0x987E4B -> the chat helper's setters (RW 0x9BE9F5 / 0x9BEA20 / 0x9BEB17)
	if (!window)
	{
		return;
	}
	if (name == "Chat")
	{
		// RW 0x9BE9F5: the list holds 1000 lines (RW 0x728867) and gets the chat history so far (RW 0x783A78: channel 1's lines, column 0, row -1)
		m_chatList = window;
		GadgetListBoxSetListLength(window, 1000);
		GadgetListBoxReset(window);
		for (const auto &line : m_chatHistory)
		{
			GadgetListBoxAddEntryText(window, line.first, line.second, -1, 0);
		}
	}
	else if (name == "ChatPlayers")
	{
		// RW 0x9BEA20: reset, 1000 lines. Not ported: the list's flag + 0x12 (RW 0x97D69E) and its fill (the chat's recipients; every line goes to every player here)
		m_chatPlayers = window;
		GadgetListBoxReset(window);
		GadgetListBoxSetListLength(window, 1000);
	}
	else if (name == "ChatEntry")
	{
		// RW 0x9BEB17: the focus (window manager slot 0xC4), at most 110 characters (RW 0x81606D with 0x6E), empty
		m_chatEntry = window;
		window->manager().winSetFocus(window);
		if (EntryData *entry = static_cast<EntryData *>(window->winGetUserData()))
		{
			entry->maxTextLen = 0x6E;
		}
		GadgetTextEntrySetText(window, std::u16string());
	}
	else
	{
		windows().note("unknown-gadget", "AptMpChat::InitGadgets(" + name + ")");
	}
}

void AptSkirmish::sendChat()
{
	// RW 0x9BEDE0: the entry's text, trimmed, the entry cleared; a non-empty text goes out (the helper's slot 0x10 with the recipients). Not ported: RW
	// 0x78A594's pass over the text (not decoded) and the recipients from ChatPlayers' selection (RW 0x9BECCB): the text goes to every player
	pruneDeadGadgets();
	if (!m_chatEntry)
	{
		windows().note("command-ignored", "AptMpChat::Send without a ChatEntry gadget");
		return;
	}
	std::u16string text = GadgetTextEntryGetText(m_chatEntry);
	GadgetTextEntrySetText(m_chatEntry, std::u16string());
	const std::size_t a = text.find_first_not_of(u" \t");
	const std::size_t b = text.find_last_not_of(u" \t");
	text = a == std::u16string::npos ? std::u16string() : text.substr(a, b - a + 1);
	if (!text.empty())
	{
		onChatSend(text);
	}
}

void AptSkirmish::addChatLine(const std::u16string &line, Color color)
{
	// RW 0x78419D: channel 1's history and every chat list showing it
	m_chatHistory.emplace_back(line, color);
	if (m_chatHistory.size() > 1000)
	{
		m_chatHistory.erase(m_chatHistory.begin());
	}
	pruneDeadGadgets();
	if (m_chatList)
	{
		GadgetListBoxAddEntryText(m_chatList, line, color, -1, 0);
	}
}

void AptSkirmish::refreshLobby()
{
	refreshAll();
	updateMapPreview();
}

void AptSkirmish::setGameMapType(const std::string &type)
{
	m_gameMapType = type;
	m_strategic = type == "Strategic"; // MpGameSetup+0x7C == 1
	if (m_setup)
	{
		m_setup->setStrategicMode(m_strategic);
	}
	for (int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		for (GameWindow *w : m_slots[slot])
		{
			if (w && slot >= 6)
			{
				w->winHide(m_strategic);
			}
		}
	}
	refreshAll();
}

GameWindow *AptSkirmish::slotGadget(int slot, const std::string &leaf) const
{
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return nullptr;
	}
	for (int i = 0; i < 6; ++i)
	{
		if (leaf == leafName(i))
		{
			GameWindow *w = m_slots[slot][i];
			// lane MP-2: a setup movie that played out (LanLobby.apt) leaves its windows behind until the next refresh prunes them
			AptGadgetLayer *layer = const_cast<AptSkirmish *>(this)->windows().gadgetLayer();
			if (w && layer)
			{
				const std::vector<GameWindow *> live = layer->gadgets().allWindows();
				w = std::find(live.begin(), live.end(), w) != live.end() ? w : nullptr;
			}
			return w;
		}
	}
	return nullptr;
}

void AptSkirmish::pruneDeadGadgets()
{
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (!layer)
	{
		return;
	}
	const std::vector<GameWindow *> live = layer->gadgets().allWindows();
	const std::set<const GameWindow *> alive(live.begin(), live.end());
	auto check = [&](GameWindow *&w) {
		if (w && !alive.count(w))
		{
			w = nullptr;
		}
	};
	check(m_mapList);
	check(m_mapInfo);
	check(m_mapDescription);
	check(m_mapPicture);
	check(m_currentMap);
	check(m_profileList);
	check(m_profileEntry);
	check(m_chatList);
	check(m_chatPlayers);
	check(m_chatEntry);
	for (auto &row : m_slots)
	{
		for (GameWindow *&w : row)
		{
			check(w);
		}
	}
	for (auto it = m_slotGadgets.begin(); it != m_slotGadgets.end();)
	{
		it = alive.count(it->first) ? std::next(it) : m_slotGadgets.erase(it);
	}
	m_currentMapChildren.erase(std::remove_if(m_currentMapChildren.begin(), m_currentMapChildren.end(), [&](GameWindow *w) { return !alive.count(w); }), m_currentMapChildren.end());
}

// ---- gadget initialisation ------------------------------------------------------------------------------------------------------------------

void AptSkirmish::initSkirmishGadget(const std::string &name, GameWindow *window)
{
	if (!window)
	{
		return;
	}
	if (name == "Skirmish::SelectProfile")
	{
		m_profileList = window;
		refreshProfileList();
	}
	else if (name == "Skirmish::CreatePersonaEntry")
	{
		// BFME1 SkirmishScreenOnInitGadget.cpp: the entry starts empty (and gets control id 11: not modelled, the screen finds it by pointer)
		m_profileEntry = window;
		GadgetTextEntrySetText(window, std::u16string());
	}
	else if (name == "Skirmish::GameInfo" || name == "Skirmish::PlayerProfile")
	{
		windows().note("unported-gadget-init", "AptSkirmish::InitGadgets(" + name + "): BFME1 name, not placed by RotWK's Skirmish.apt [S-175]");
	}
	else
	{
		windows().note("unknown-gadget", "AptSkirmish::InitGadgets(" + name + ")");
	}
}

void AptSkirmish::initMpGameSetupGadget(const std::string &name, GameWindow *window)
{
	// MpGameSetup::InitGadgets (RW 0x840906; open-bfme-2 calls it _bfme_onInitGadget)
	if (!window)
	{
		return;
	}
	if (name == "MapList")
	{
		m_mapList = window;
		const int widths[5] = { 8, 2, 0x46, 10, 10 };
		GadgetListBoxSetColumnWidths(window, 5, widths);
		populateMapList();
		return;
	}
	unsigned index = 0;
	if (std::sscanf(name.c_str(), "%u", &index) != 1)
	{
		return; // retail: sscanf != 1 -> return without a note
	}
	if (index > 8)
	{
		return;
	}
	if (index >= (unsigned)MAX_SLOTS)
	{
		// the retail check is `index > 8`: index 8 passes and the store `m_player[8]` lands in the next array (a retail overrun); not reproduced
		windows().note("gadget-slot-out-of-range", "MpGameSetup::InitGadgets(" + name + "): slot " + std::to_string(index));
		return;
	}
	const std::size_t marker = name.find_last_of("~/");
	const std::string leaf = marker == std::string::npos ? name : name.substr(marker + 1);
	int which = -1;
	for (int i = 0; i < 6; ++i)
	{
		if (leaf == leafName(i))
		{
			which = i;
		}
	}
	if (which < 0)
	{
		return;
	}
	m_slots[index][which] = window;
	m_slotGadgets[window] = SlotGadget{ (int)index, (Leaf)which };
	populateSlotGadget((int)index, (Leaf)which, window);
	if (index >= 6 && m_strategic)
	{
		window->winHide(true);
	}
}

void AptSkirmish::initMapPreviewGadget(const std::string &name, GameWindow *window)
{
	// AptMapPreview::mapGadgetInit (BFME1 0x00521990)
	if (!window)
	{
		return;
	}
	if (name == "MapInfo")
	{
		m_mapInfo = window;
		window->winEnable(true);
		GadgetListBoxSetAudioFeedback(window, true);
	}
	else if (name == "MapDescription")
	{
		m_mapDescription = window;
		window->winEnable(true);
		GadgetListBoxSetAudioFeedback(window, true);
	}
	else if (name == "MapPicture")
	{
		m_mapPicture = window;
	}
	else if (name == "CurrentMap")
	{
		m_currentMap = window;
		m_currentMapChildren.clear();
		for (GameWindow *child = window->winGetChild(); child && m_currentMapChildren.size() < 8; child = child->winGetNext())
		{
			m_currentMapChildren.push_back(child);
			child->winHide(true);
			child->winSetStatus(WIN_STATUS_RIGHT_CLICK); // RW 0x9778B0: status | 0x20000
		}
	}
	updateMapPreview();
}

// ---- lobby ----------------------------------------------------------------------------------------------------------------------------------------

void AptSkirmish::populateSlotGadget(int slot, Leaf leaf, GameWindow *combo)
{
	if (leaf == Leaf::Color)
	{
		GadgetImageComboBoxReset(combo);
	}
	else
	{
		GadgetComboBoxReset(combo);
	}
	if (!m_setup)
	{
		return;
	}
	m_refreshing = true;
	switch (leaf)
	{
		case Leaf::Player:
			fillCombo(combo, m_setup->playerEntries(slot));
			break;
		case Leaf::PlayerTemplate:
			fillCombo(combo, m_setup->templateEntries());
			break;
		case Leaf::Team:
			fillCombo(combo, m_setup->teamEntries());
			break;
		case Leaf::Color:
			fillImageCombo(combo, m_setup->colorEntries(slot));
			break;
		case Leaf::Handicap:
		case Leaf::Hero:
			// RotWK fills these from sources the engine does not have [S-178]
			break;
	}
	m_refreshing = false;
	refreshSlot(slot);
}

namespace
{
// StringBase<unsigned short>::compareNoCase (RW 0x406678 as called by RW 0x84246C): three-way, case-insensitive (ASCII letters; INFERENCE: the
// CRT's case folding beyond ASCII is not reproduced)
int compareNoCase(const std::u16string &a, const std::u16string &b)
{
	const std::size_t n = std::min(a.size(), b.size());
	for (std::size_t i = 0; i < n; ++i)
	{
		char16_t x = a[i], y = b[i];
		x = (x >= u'A' && x <= u'Z') ? (char16_t)(x - u'A' + u'a') : x;
		y = (y >= u'A' && y <= u'Z') ? (char16_t)(y - u'A' + u'a') : y;
		if (x != y)
		{
			return x < y ? -1 : 1;
		}
	}
	return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}
} // namespace

void AptSkirmish::setMapSort(int mode)
{
	// RW 0x84010E (OnSortName pushes 0, OnSortPlayers 2, OnSortIcons 4: RW 0x840143 / 0x84014D / 0x840157): the same column again flips its
	// direction (mode + 1); a new column becomes the primary key and the old one the secondary unless it was the flipped form of the new one;
	// then the list is marked for a refill (+0x2BC = 1).  INFERENCE: the refill runs at once here (retail: the screen update reads +0x2BC).
	if (m_sortPrimary == mode)
	{
		m_sortPrimary = mode + 1;
	}
	else
	{
		const int previous = m_sortPrimary;
		m_sortPrimary = mode;
		if (previous != mode + 1)
		{
			m_sortSecondary = previous;
		}
	}
	populateMapList();
}

void AptSkirmish::populateMapList()
{
	// MpGameSetup's map list fill (RW 0x8460B5; the column widths {8, 2, 70, 10, 10} come from MpGameSetup::InitGadgets RW 0x840906):
	//   * every map gets a conquest state (+0xF4): 0 without the battle honors (RotWK reads them only with flag +0x3A4 bit 0; S-178: none here) or for a
	//     map that is not multiplayer; a map that is not official (+0x26 == 0) has 0x8000 added;
	//   * the maps are sorted by RW 0x84606C with the comparator RW 0x84246C: the primary key +0x3AC (2 at construction, RW 0x8451A9), then the
	//     secondary +0x3B0 (0), then name, player count, state and finally the record address (here: the cache order);
	//   * per map, RW 0x846674 ..: the state's image (AptDifficultyNotConquered / AptUserMapNotConquered for state 0 / 0x8000; the conquered
	//     images need the honors) in column 0, width = height = min(AptDifficultyHardConquered's width (10 without it), column 0's width); the
	//     display name WITHOUT the player count (RW 0x702453 with 0) in column numColumns - 3; the player count (L"%d", RW 0xBDF1B0) in column
	//     numColumns - 1; the map key appended to the row's key list (+0x398, RW 0x42D8EE).  Every text is colour -1 (white).
	//   Not reproduced: the call RW 0x726DD4(list, state, -1, 1) before the row exists (an item data on row -1).
	if (!m_mapList || !m_setup)
	{
		return;
	}
	m_refreshing = true;
	GadgetListBoxReset(m_mapList);
	m_mapKeys.clear();
	const std::vector<MapCacheEntry> &maps = m_setup->source().maps();
	struct Row
	{
		int index;
		int state;
		std::u16string countedName; // the comparator's name: RW 0x702453 with 1 (the player count appended)
	};
	std::vector<Row> rows;
	for (int index : m_setup->listedMaps())
	{
		const MapCacheEntry &m = maps[(std::size_t)index];
		const int state = m.isOfficial ? 0 : 0x8000;
		rows.push_back(Row{ index, state, m_setup->mapDisplayName(m, true) });
	}
	const int keys[2] = { m_sortPrimary, m_sortSecondary };
	std::stable_sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) {
		const int players = maps[(std::size_t)a.index].numPlayers - maps[(std::size_t)b.index].numPlayers;
		const int name = compareNoCase(a.countedName, b.countedName);
		const int state = a.state - b.state;
		for (int key : keys)
		{
			switch (key)
			{
				case 0:
					if (name != 0)
						return name < 0;
					break;
				case 1:
					if (name != 0)
						return name > 0;
					break;
				case 2:
					if (players != 0)
						return players < 0;
					break;
				case 3:
					if (players != 0)
						return players > 0;
					break;
				case 4:
					if (state != 0)
						return state < 0;
					break;
				case 5:
					if (state != 0)
						return state > 0;
					break;
				default:
					break;
			}
		}
		if (name != 0)
			return name < 0;
		if (players != 0)
			return players < 0;
		return state < 0;
	});
	const int numColumns = GadgetListBoxGetNumColumns(m_mapList);
	if (numColumns < 3)
	{
		// retail writes to column numColumns - 3 unchecked: a mod's list with fewer columns has no retail result to match
		windows().note("map-list-columns", "MpGameSetup MapList has " + std::to_string(numColumns) + " columns; RW 0x8460B5 needs 3 or more");
		m_refreshing = false;
		return;
	}
	const MappedImageCollection *images = m_mapList->manager().images();
	const Image *hard = images ? images->findImageByName("AptDifficultyHardConquered") : nullptr;
	const int size = std::min(hard ? hard->getImageWidth() : 10, GadgetListBoxGetColumnWidth(m_mapList, 0));
	const Color white = 0xFFFFFFFFu;
	int selection = -1;
	for (const Row &r : rows)
	{
		const MapCacheEntry &m = maps[(std::size_t)r.index];
		const char *image = (r.state & 0x8000) ? "AptUserMapNotConquered" : "AptDifficultyNotConquered";
		int row = GadgetListBoxAddEntryImage(m_mapList, image, -1, 0, size, size, true, white);
		row = GadgetListBoxAddEntryText(m_mapList, m_setup->mapDisplayName(m, false), white, row, numColumns - 3);
		char16_t count[16];
		const std::string digits = std::to_string(m.numPlayers);
		std::size_t n = 0;
		for (char c : digits)
		{
			count[n++] = (char16_t)c;
		}
		GadgetListBoxAddEntryText(m_mapList, std::u16string(count, n), white, row, numColumns - 1);
		m_mapKeys.push_back(m.name);
		if (m.name == m_setup->info().mapName)
		{
			selection = row;
		}
	}
	// INFERENCE: RW 0x8460B5 does not select; the lobby shows the game's map selected (the map preview names it)
	if (selection >= 0)
	{
		GadgetListBoxSetSelected(m_mapList, selection);
	}
	m_refreshing = false;
}

void AptSkirmish::refreshColors(int slot)
{
	GameWindow *combo = m_slots[slot][(int)Leaf::Color];
	if (!combo || !m_setup)
	{
		return;
	}
	fillImageCombo(combo, m_setup->colorEntries(slot));
	selectImageByItemData(combo, m_setup->info().slots[slot].color);
}

void AptSkirmish::refreshSlot(int slot)
{
	if (!m_setup || slot < 0 || slot >= MAX_SLOTS)
	{
		return;
	}
	const bool outer = m_refreshing;
	if (!outer)
	{
		pruneDeadGadgets(); // lane MP-2: a setup movie played out and in again (LanLobby's CancelGame / DoJoinGame) left the old instance's windows behind
	}
	m_refreshing = true;
	const SkirmishGameSlot &s = m_setup->info().slots[slot];
	if (GameWindow *combo = m_slots[slot][(int)Leaf::Player])
	{
		const int kind = m_setup->slotShowsName(slot) ? 1 : 0;
		if (m_setup->network() && m_playerComboKind[(size_t)slot] != kind)
		{
			// lane MP-2: a player joined or left the slot: its entries change (the name, or the slot states)
			m_playerComboKind[(size_t)slot] = kind;
			GadgetComboBoxReset(combo);
			fillCombo(combo, m_setup->playerEntries(slot));
		}
		if (m_setup->network())
		{
			for (GameWindow *w : m_slots[slot])
			{
				if (w)
				{
					w->winEnable(lobbyEditable(slot));
				}
			}
		}
		if (kind == 1)
		{
			// the local player: one blank entry, the player's name as the text (BFME1 refreshGameSlot: GadgetComboBoxSetText for a human slot)
			GadgetComboBoxSetText(combo, s.name);
		}
		else
		{
			selectByItemData(combo, (int)s.state);
		}
	}
	if (GameWindow *combo = m_slots[slot][(int)Leaf::PlayerTemplate])
	{
		selectByItemData(combo, s.playerTemplate);
	}
	if (GameWindow *combo = m_slots[slot][(int)Leaf::Team])
	{
		selectByItemData(combo, s.teamNumber);
	}
	refreshColors(slot);
	m_refreshing = outer;
}

void AptSkirmish::refreshAll()
{
	pruneDeadGadgets();
	if (!m_setup)
	{
		return;
	}
	const bool outer = m_refreshing;
	m_refreshing = true;
	for (int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		refreshSlot(slot);
	}
	m_refreshing = outer;
}

void AptSkirmish::updateMapPreview()
{
	pruneDeadGadgets();
	// AptMapPreview::rva005217A0 -> rva005216B0 (title, MapInfo, description, picture)
	if (!m_setup)
	{
		return;
	}
	const MapCacheEntry *map = nullptr;
	for (const MapCacheEntry &m : m_setup->source().maps())
	{
		if (m.name == m_setup->info().mapName)
		{
			map = &m;
			break;
		}
	}
	windows().setAptText("APT:MapTitle", map ? u16ToUtf8(m_setup->mapDisplayName(*map, true)) : std::string(" "));
	// lane UI-2 (owner feedback F5, "no map preview"): AptMapPreview's picture (RW 0x97640C -> 0x975F23): the map file name without its last four
	// characters plus "_pic.tga" when the file system has it (a 128 x 128 image of that texture, UV 0..1), else the mapped image MissingMap; the
	// render callback AptMapPreview::Picture (RW 0x9757C0) draws it over its clip (the device: WindowManager::renderPicture)
	{
		WindowManager::RenderPicture picture;
		picture.mappedImage = "MissingMap";
		if (map && map->name.size() > 4)
		{
			std::string file = map->name.substr(0, map->name.size() - 4) + "_pic.tga";
			std::replace(file.begin(), file.end(), '/', '\\');
			if (!m_setup->source().hasFileSystem())
			{
				windows().note("map-picture", "the setup source has no file system: " + file + " cannot be looked up, MissingMap is shown");
			}
			else if (m_setup->source().fileExists(file))
			{
				picture.mappedImage.clear();
				picture.file = file;
			}
		}
		windows().setRenderPicture("AptMapPreview::Picture", picture);
	}
	// lane UI-2 (owner feedback F5, "no map name"): RotWK's map-info update RW 0x976497 (MapInfo present, not the strategic lobby) sets
	// APT:CurrentMapName to the map's display name without the player count (RW 0x7022BF) and APT:LobbyGameType to the binary's own literal
	// u"Free For All" (RW 0xC84F30); with no map both are the empty string (RW 0x976748 .. 0x9767C0)
	if (m_mapInfo)
	{
		windows().setAptText("APT:CurrentMapName", map ? u16ToUtf8(m_setup->mapDisplayName(*map, false)) : std::string());
		windows().setAptText("APT:LobbyGameType", map ? std::string("Free For All") : std::string());
	}
	updateCurrentMapWindow(map);
	if (m_mapInfo)
	{
		GadgetListBoxReset(m_mapInfo);
		if (map)
		{
			GadgetListBoxAddEntryText(m_mapInfo, m_setup->mapDescription(*map), 0xFFFFFFFFu, -1, -1, true);
			m_mapInfo->winEnable(true);
		}
	}
	if (m_mapDescription)
	{
		GadgetListBoxReset(m_mapDescription);
		if (map)
		{
			GadgetListBoxAddEntryText(m_mapDescription, m_setup->mapDescriptionFirstLine(*map), 0xFFFFFFFFu, -1, -1, true);
		}
	}
}

std::string AptSkirmish::mapPreviewImage(const MapCacheEntry &map)
{
	// RW 0x702689 (getMapPreviewImage): the map file name without its last four characters; the image is named after that path with '\\' and ':'
	// made '_' and is looked up first; else "<path>_art.tga" when the file system has it (UV (0,1)-(1,0)), else "<path>.tga" (UV (0,0)-(1,1)); RW
	// 0x70292F makes a 128 x 128 raw-texture image of it (status 2) and adds it to the mapped images. INFERENCE [S-1480]: retail first copies the
	// file into the user data folder ("%sMapPreviews\\", RW 0x701F81) and loads the copy; the port reads the file where it is mounted, and the
	// name's FUN_006DD947 transform of the path (not read) is taken as the identity.
	GameWindowManager &mgr = m_currentMap->manager();
	MappedImageCollection *images = mgr.mutableImages();
	if (!images || map.name.size() <= 4)
	{
		return std::string();
	}
	std::string base = map.name.substr(0, map.name.size() - 4);
	std::replace(base.begin(), base.end(), '/', '\\');
	std::string name = base;
	for (char &c : name)
	{
		if (c == '\\' || c == ':')
		{
			c = '_';
		}
	}
	if (images->findImageByName(name))
	{
		return name;
	}
	if (!m_setup->source().hasFileSystem())
	{
		windows().note("map-preview", "the setup source has no file system: the preview of " + base + " cannot be looked up");
		return std::string();
	}
	Image image;
	image.name = name;
	image.status = IMAGE_STATUS_RAW_TEXTURE;
	image.textureSize.x = image.textureSize.y = 128;
	// retail UV -> the port's top-row-first textures: v' = 1 - v (WindowManager::RenderPicture)
	if (m_setup->source().fileExists(base + "_art.tga"))
	{
		image.filename = base + "_art.tga";
		image.uvLo[0] = 0.0f;
		image.uvLo[1] = 0.0f; // retail (0, 1)
		image.uvHi[0] = 1.0f;
		image.uvHi[1] = 1.0f; // retail (1, 0)
	}
	else if (m_setup->source().fileExists(base + ".tga"))
	{
		image.filename = base + ".tga";
		image.uvLo[0] = 0.0f;
		image.uvLo[1] = 1.0f; // retail (0, 0)
		image.uvHi[0] = 1.0f;
		image.uvHi[1] = 0.0f; // retail (1, 1)
	}
	else
	{
		return std::string();
	}
	images->addImage(image);
	return name;
}

void AptSkirmish::updateCurrentMapWindow(const MapCacheEntry *map)
{
	// RW 0x9772E6 (the lobby, not the strategic one: +0x1C != 1) on the CurrentMap window (+0x20)
	if (!m_currentMap)
	{
		return;
	}
	GameWindow *window = m_currentMap;
	window->winSetEnabledImage(1, std::string());
	std::string image;
	if (map)
	{
		image = mapPreviewImage(*map);
		const Image *made = image.empty() ? nullptr : window->manager().images()->findImageByName(image);
		const std::string art = "_art.tga";
		if (made && made->filename.size() >= art.size() && asciiLower(made->filename.substr(made->filename.size() - art.size())) == art)
		{
			window->winSetEnabledImage(1, "ScrollShroud"); // RW 0x97745A: the radar's shroud over an _art picture
		}
	}
	window->winSetUserData(const_cast<MapCacheEntry *>(map)); // W3DDrawMapPreview reads the map (RW 0x97749A)
	if (image.empty())
	{
		image = "MissingMap"; // RW 0x9774C6
	}
	if (window->manager().images() && window->manager().images()->findImageByName(image))
	{
		window->winSetStatus(WIN_STATUS_IMAGE);
		window->winSetEnabledImage(0, image);
	}
	else
	{
		window->winClearStatus(WIN_STATUS_IMAGE); // RW 0x977522
	}
	// RW 0x976F59: the start spots
	GameWindow *buttons[8] = {};
	for (std::size_t i = 0; i < m_currentMapChildren.size() && i < 8; ++i)
	{
		buttons[i] = m_currentMapChildren[i];
	}
	int shown = 0;
	if (map)
	{
		window->winEnable(true);
		// RW 0x976F9C .. 0x976FB6: only for a multiplayer map (MapMetaData + 0x24) when TheGameLogic + 0x114 is 3 (a skirmish). INFERENCE [S-1480]: the
		// shell's game kind is taken as 3 in the skirmish lobby and not in a LAN lobby (+0x114 is 1 for a LAN game, RW 0x779CB4)
		if (map->isMultiplayer && !m_lan && map->numPlayers > 0)
		{
			for (; shown < map->numPlayers && shown < 8; ++shown)
			{
				auto spot = map->startPositions.find(shown + 1); // "Player_%d_Start"
				if (spot == map->startPositions.end())
				{
					continue;
				}
				GameWindow *button = buttons[shown];
				MapPreviewPositionStartSpot(button, window, spot->second, *map, buttons);
				if (button)
				{
					button->winEnable(true); // RW 0x976FFE: the screen's flag + 0x61 (INFERENCE [S-1480]: enabled)
					button->winHide(false);
				}
			}
		}
	}
	else
	{
		window->winEnable(false);
	}
	for (int i = shown; i < 8; ++i)
	{
		if (buttons[i])
		{
			buttons[i]->winHide(true);
			GadgetButtonSetText(buttons[i], fetchOrMissing(m_env.gameText, "GUI:Blank")); // RW 0x977047
		}
	}
}

WindowMsgHandledType AptSkirmish::gadgetMessage(GameWindow *, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	pruneDeadGadgets();
	GameWindow *gadget = reinterpret_cast<GameWindow *>(data1);
	switch (msg)
	{
		case GCM_SELECTED:
			onComboSelected(gadget);
			return MSG_HANDLED;
		case GEM_UPDATE_TEXT:
			// RotWK 0x929589: the persona entry's text decides whether the popup's accept button is enabled (raw text, not trimmed, not a duplicate test)
			if (gadget == m_profileEntry && gadget)
			{
				if (GadgetTextEntryGetText(m_profileEntry).empty())
				{
					if (!m_acceptDisabled)
					{
						invokeRoot("PopupSelectBttnDisable");
						m_acceptDisabled = true;
					}
				}
				else if (m_acceptDisabled)
				{
					invokeRoot("PopupSelectBttnEnable");
					m_acceptDisabled = false;
				}
				return MSG_HANDLED;
			}
			return MSG_IGNORED;
		case GEM_EDIT_DONE:
			if (gadget == m_profileEntry && gadget && data2 == 0)
			{
				acceptAction();
				return MSG_HANDLED;
			}
			if (gadget == m_chatEntry && gadget)
			{
				sendChat(); // lane UI-1, INFERENCE: Enter in the chat entry is the movie's Send button (ZH's chat entry sends on GEM_EDIT_DONE)
				return MSG_HANDLED;
			}
			return MSG_IGNORED;
		case GLM_DOUBLE_CLICKED:
			if (gadget == m_profileList && gadget)
			{
				// the list sends the message while its own selection is mid-update (ZH GadgetListBox: selectPos is -2 until the click is resolved), so the
				// double-clicked row (data2) is selected first; the observable result is the same as retail's accept on the clicked profile [S-179]
				if ((int)(std::intptr_t)data2 >= 0)
				{
					GadgetListBoxSetSelected(m_profileList, (int)(std::intptr_t)data2);
				}
				acceptAction();
				return MSG_HANDLED;
			}
			return MSG_IGNORED;
		case GLM_SELECTED:
			if (gadget == m_mapList)
			{
				onMapListSelected((int)(std::intptr_t)data2);
				return MSG_HANDLED;
			}
			return MSG_IGNORED;
		default:
			return MSG_IGNORED;
	}
}

void AptSkirmish::onComboSelected(GameWindow *combo)
{
	if (m_refreshing || !m_setup)
	{
		return;
	}
	auto it = m_slotGadgets.find(combo);
	if (it == m_slotGadgets.end())
	{
		return;
	}
	int selected = -1;
	const bool image = it->second.leaf == Leaf::Color;
	if (image)
	{
		GadgetImageComboBoxGetSelectedPos(combo, &selected);
	}
	else
	{
		GadgetComboBoxGetSelectedPos(combo, &selected);
	}
	if (selected < 0)
	{
		return;
	}
	const int value = itemValue(image ? GadgetImageComboBoxGetItemData(combo, selected) : GadgetComboBoxGetItemData(combo, selected));
	const int slot = it->second.slot;
	if (!lobbyEditable(slot))
	{
		refreshAll(); // lane MP-2: a slot the local player may not change shows the model again
		return;
	}
	switch (it->second.leaf)
	{
		case Leaf::Player:
			// handlePlayerSelection: the local slot (0; lane MP-2: and a network game's humans) is not selectable; SLOT_PLAYER is never an entry of a player combo
			if (!m_setup->slotShowsName(slot) && (SlotState)value != SLOT_PLAYER)
			{
				m_setup->setSlotState(slot, (SlotState)value, GadgetComboBoxGetText(combo));
			}
			break;
		case Leaf::PlayerTemplate:
			m_setup->setSlotTemplate(slot, value);
			break;
		case Leaf::Team:
			m_setup->setSlotTeam(slot, value);
			break;
		case Leaf::Color:
			m_setup->setSlotColor(slot, value);
			break;
		case Leaf::Handicap:
		case Leaf::Hero:
			break;
	}
	refreshAll();
	lobbyChanged();
}

void AptSkirmish::onMapListSelected(int row)
{
	if (m_refreshing || !m_setup || row < 0)
	{
		return;
	}
	if (!lobbyEditable(-1))
	{
		return; // lane MP-2: only the host chooses the map
	}
	// the row's map key from the list kept beside the rows (MpGameSetup +0x398, filled by RW 0x8460B5)
	if (row >= (int)m_mapKeys.size())
	{
		windows().note("map-list-row-without-data", "row " + std::to_string(row));
		return;
	}
	const std::string key = m_mapKeys[(std::size_t)row];
	if (!m_setup->setMap(key))
	{
		windows().note("map-unknown", key);
		return;
	}
	refreshAll();
	updateMapPreview();
	lobbyChanged();
}

bool AptSkirmish::startGame()
{
	pruneDeadGadgets();
	if (!m_setup || !m_env.newGame)
	{
		windows().note("unported-command", "AptSkirmish::StartGame: the lobby has no model or no NewGameSink");
		return false;
	}
	if (m_setup->info().inProgress)
	{
		windows().note("start-ignored", "AptSkirmish::StartGame: a game was already started from this lobby");
		return false;
	}
	// startPressed -> reallyDoStart (ZH SkirmishGameOptionsMenu.cpp:506 / 418).  [S-179] Whether a missing profile blocks the start is not known; it
	// does not here.
	std::u16string title, text;
	if (!m_setup->validateStart(title, text))
	{
		m_env.newGame->startRefused(title, text);
		return false;
	}
	const NewGameMessage message = m_setup->makeNewGameMessage();
	const std::vector<std::string> unresolved = message.game.unresolvedRandomChoices();
	if (!unresolved.empty())
	{
		// [S-178] nothing resolves these yet; the consumer must do it in retail's order (SkirmishSetup.h, RANDOM CHOICES) and must not substitute defaults
		std::string list;
		for (const std::string &u : unresolved)
		{
			list += (list.empty() ? "" : ", ") + u;
		}
		windows().note("skirmish-random-unresolved", "[S-178] the new-game message carries unresolved random choices (" + list + "); no consumer resolves them yet");
	}
	m_env.newGame->queueNewGame(message);
	return true;
}

// ---- profiles -------------------------------------------------------------------------------------------------------------------------------

void AptSkirmish::applyProfileName()
{
	pruneDeadGadgets();
	if (!m_setup)
	{
		return;
	}
	m_setup->setPlayerName(playerName());
	refreshSlot(0);
}

void AptSkirmish::refreshProfileList()
{
	// lane END-1: a covered or closing screen (runShutdown: the load screen pushed over it, the shell popping it) leaves its gadgets alone; its movie still runs
	// OnClosed while the gadgets are torn down, which wrote into a destroyed list box when a profile existed (a second visit of the session)
	if (isHidden())
	{
		return;
	}
	pruneDeadGadgets();
	// BFME1 SkirmishScreenRefreshProfile.cpp: reset, one row per profile, the current one selected
	if (!m_profileList)
	{
		return;
	}
	GadgetListBoxReset(m_profileList);
	const std::vector<std::u16string> names = m_profiles->names();
	const std::u16string current = m_profiles->current();
	int selection = -1;
	for (std::size_t i = 0; i < names.size(); ++i)
	{
		const int row = GadgetListBoxAddEntryText(m_profileList, names[i], 0xFFFFFFFFu, -1, 0);
		if (names[i] == current)
		{
			selection = row;
		}
	}
	if (selection >= 0)
	{
		GadgetListBoxSetSelected(m_profileList, selection);
	}
}
