// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptSkirmish.h.

#include "Common/BfmeGame.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"

#include "GameClient/GUI/AptScreens/AptStats.h"
#include "GameClient/GUI/Skirmish/SkirmishGameRecord.h"
#include "GameClient/GUI/Skirmish/RealTimeStats.h"

#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/OptionPreferences.h"
#include "Common/AsciiString.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameClient/GUI/AptScreens/AptCreateAHero.h"
#include "GameClient/CreateAHeroHeroList.h"

#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/PlatformIcons.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
	destroySlotScroll();
	if (AptGadgetLayer *layer = windows().gadgetLayer()) layer->destroyEngineGadget(m_mapCapacityFilter);
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
		if (environment.options)
		{
			m_setup->setSimulationProfile(environment.options->simulationProfile()); // lane UBER-1: the OpenBFME option Uber performance mode
		}
		if (!m_setup->init(playerName(), environment.newGame->newGameSeed(), std::string(), &m_error))
		{
			windows.note("skirmish-setup-failed", m_error);
			m_setup.reset();
		}
		else if (!lan)
		{
			restoreGame(0); // lane UI-6: a skirmish lobby (mode 0) until the movie says Strategic (setGameMapType)
		}
	}
	// lane UI-2: the map window's inferences (GadgetMapPreview.cpp, AptSkirmish::updateCurrentMapWindow)
	windows.note("map-preview-inferences", "[S-1480] map window: the preview file is read where it is mounted (retail copies it to MapPreviews first), its image name is the path with '\\' and ':' as '_', start spots are shown in every lobby that is not a War of the Ring battle's (TheGameLogic + 0x114 == 3, the battle kind) and enabled, an APT gadget's parent is its owner; a start-spot click is handled as RW 0x845830 (FB7-1, S-1915)");
	if (m_lan)
	{
		registerLobby(); // lane MP-2: AptLanLobby registers its own commands and update
		return;
	}
	registerAll();
	if (m_setup)
	{
		windows.note("skirmish-inferences", "[S-178] lobby contents inferred: faction rule, AI on every map, no honors stars, the Hero combo's inferences (S-1406), the rule rows' row generation and drop-down disable (S-3600), ImageComboBox behaviour, `_global.InGame` set by the manager");
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
	// lane FB7-1: Back and Exit are one handler in RotWK (RW 0x929920 / 0x9298CB both register RW 0x9280E8 -> 0x927FDA): with the screen alive
	// (RW 0xDEA398) it sets the window manager's pop request (RW 0x62215B: WindowManager + 0x310); the lobby's MAIN MENU button sends Exit
	registerCommand("AptSkirmish::Back", [this](const std::string &) { windows().requestShellPop(); });
	registerCommand("AptSkirmish::StartGame", [this](const std::string &) {
		// RotWK 0x9280F0: only the state is set; the next screen update validates and posts the game
		m_profileState = kStateStart;
	});
	registerCommand("AptSkirmish::Exit", [this](const std::string &) { windows().requestShellPop(); });
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
	// lane MENU-1: RW 0x9280FD (state 8; the update makes it 9) and RW 0x9281C0 (8 or 9 -> 7). The profile page itself is Skirmish.apt's: its
	// _open_profile loads Stats.swf into SkirmishStats once _close has closed the game setup (_root[vScreenName]); the page's contents (AptStats'
	// registrations) are not ported [S-175]
	registerCommand("AptSkirmish::OnStatsMenu", [this](const std::string &) {
		m_profileState = kStateStats;
		// lane ONLINEHUB-1: Stats.swf's registrations (AptStats, RW 0x9F021E) before the movie loads it: the player's own statistics from the games
		// this installation recorded (GameClient/GameRecords.h); RotWK's skirmish profile records are not read [S-4874]
		if (!m_lan && !m_statsPage)
		{
			m_statsPage = std::make_unique<AptStats>(windows(), m_env, AptStats::Host::Skirmish, ownerWindow());
			m_statsPage->setLevel(level());
			m_statsPage->setMapNames([this](const std::string &key) {
				const MapCacheEntry *m = m_setup ? m_setup->findMap(key) : nullptr;
				return m ? m_setup->mapBaseDisplayName(*m) : std::u16string(key.begin(), key.end());
			});
		}
	});
	registerCommand("AptSkirmish::OnExitStatsScreen", [this](const std::string &) {
		if (m_profileState == kStateStats || m_profileState == kStateStats + 1)
		{
			m_profileState = kStateClosed;
		}
		m_statsPage.reset();
	});
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
	// BFME1's persona popup (Open-BFME-1 AptSkirmishConstructor.cpp:380-387 registers these under the screen's prefix): accept adds the typed
	// persona as OnAddProfileAccept does, cancel closes as OnProfilePopupCancel, remove deletes the selected one; PersonaOk closes an error page
	if (m_env.fileSystem && isBfme1Data(*m_env.fileSystem)) // community PR #36: BFME1 data only
	{
	registerCommand("AptSkirmish::Skirmish::PersonaAccept", [this](const std::string &) {
		pruneDeadGadgets();
		if (m_profileState != kStateNewProfile && m_profileState != kStateChangeProfile)
			m_profileState = kStateNewProfile;
		acceptAction();
	});
	registerCommand("AptSkirmish::Skirmish::PersonaCancel", [this](const std::string &) {
		pruneDeadGadgets();
		if (m_profileEntry)
			GadgetTextEntrySetText(m_profileEntry, std::u16string());
		closePopupState();
	});
	registerCommand("AptSkirmish::Skirmish::PersonaRemove", [this](const std::string &) {
		pruneDeadGadgets();
		deleteProfileFromList();
	});
	registerCommand("AptSkirmish::Skirmish::PersonaOk", [](const std::string &) {});
	}
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
	std::vector<std::string> args;
	if (m_env.fileSystem && isBfme1Data(*m_env.fileSystem))
	{
		// BFME1's Skirmish.apt (docs/BFME1.md): OpenDefault plays the lobby in ("now Open normal"), PopUpPersona opens the persona popup and
		// PopUpControl("close") closes it; it has no accept-button toggle. CloseMain and ButtonReset keep their names
		const std::string f = function;
		if (f == "ShowMain")
			function = "OpenDefault";
		else if (f == "ShowAddProfile")
			function = "PopUpPersona";
		else if (f == "CloseProfilePopup")
		{
			function = "PopUpControl";
			args.push_back("close");
		}
		else if (f == "PopupSelectBttnDisable")
			return;
	}
	if (level() < 0 || !windows().invokeAS(level(), function, args, nullptr, &err))
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
	const std::u16string previous = m_profiles->current();
	if (!m_profiles->add(name))
	{
		windows().note("profile-rejected", name.empty() ? "an empty profile name" : "a profile of that name exists: " + u16ToUtf8(name));
		return false;
	}
	// lane UI-6 review: a new profile becomes the current one as a profile change does (RW 0x9293F5): the outgoing profile keeps its game first, and the
	// lobby is set up again for the new one (it has no kept game: the defaults)
	if (!previous.empty())
	{
		keepGame();
	}
	m_profiles->setCurrent(name);
	if (!previous.empty())
	{
		reloadGameForProfile();
	}
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
	const std::u16string name = GadgetListBoxGetText(m_profileList, selected, 0);
	// lane UI-6: RW 0x9293F5 (BFME2 decomp AptSkirmishCallbacks.cpp OnChangeProfile): another profile keeps the current one's game first (RW 0x929440),
	// and its own game is set up again (state 5: InitTheGame, RW 0x9288D0)
	// The list may outlive an external profile deletion. A stale row is not a successful profile change:
	// keep the active lobby and the popup open instead of reinitialising it for the old profile.
	const std::vector<std::u16string> names = m_profiles->names();
	if (std::find(names.begin(), names.end(), name) == names.end())
	{
		windows().note("profile-rejected", "the selected profile no longer exists: " + u16ToUtf8(name));
		refreshProfileList();
		return false;
	}
	const bool other = name != m_profiles->current();
	if (other)
	{
		keepGame();
	}
	if (!m_profiles->setCurrent(name))
	{
		windows().note("profile-rejected", "cannot select profile: " + u16ToUtf8(name));
		refreshProfileList();
		return false;
	}
	if (other)
	{
		reloadGameForProfile();
	}
	applyProfileName();
	refreshProfileList();
	return true;
}

void AptSkirmish::restoreGame(int mode)
{
	// RW 0x9288D0 at 0x9289AD: SkirmishPreferences' record (RW 0x821C29: HeroIndexes, then ParseAsciiStringToGameInfo of GameInfo); a record that does not
	// parse leaves the defaults (the function's false), as does one this lobby cannot use (S-3701)
	m_restoredGame.clear();
	if (!m_setup || !m_env.skirmish || !m_env.newGame)
	{
		return;
	}
	std::string map = m_profiles->profileValue(mode, "Map");
	for (char &c : map)
	{
		c = c == '\\' ? '/' : (c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c);
	}
	// community PR #36, BFME1 data only (docs/BFME1.md): the preferred map is kept even when the rest of the saved lobby cannot be reused
	if (!map.empty() && m_env.fileSystem && isBfme1Data(*m_env.fileSystem))
	{
		m_setup->setMap(map);
	}
	const std::string record = m_profiles->profileValue(mode, "GameInfo");
	if (record.empty())
	{
		return;
	}
	SkirmishGameRecordLimits limits;
	limits.numColors = (int)m_env.skirmish->colors().size();
	limits.numTemplates = (int)m_env.skirmish->factions().size();
	SkirmishGameInfo saved;
	int savedMode = 0;
	std::string error;
	if (!ParseSkirmishGameInfo(record, limits, saved, &savedMode, &error))
	{
		windows().note("skirmish-kept-game", "[S-3701] the profile's kept game does not parse (" + error + "): the lobby starts from the defaults");
		return;
	}
	if (!m_setup->restoreSaved(saved, map, m_env.newGame->newGameSeed(), &error))
	{
		windows().note("skirmish-kept-game", "[S-3701] " + error + ": the lobby starts from the defaults");
		return;
	}
	m_restoredGame = record;
	restoreExtensions(mode);
	windows().note("skirmish-kept-game", "[S-3701] the lobby began from the profile's kept game; not kept: the War of the Ring scenario (written as "
										 "retail's empty values); OpenBFME's Legendary Heroes and the Create-a-Hero choices are kept in keys of their own");
}

void AptSkirmish::reloadGameForProfile()
{
	// lane UI-6: AptSkirmish::InitTheGame again (state 5, RW 0x9288D0) for the profile now current: the defaults, then its kept game
	m_slotHero.fill(-1);
	if (m_lan || !m_setup || !m_env.newGame)
	{
		return;
	}
	std::string error;
	if (!m_setup->init(playerName(), m_env.newGame->newGameSeed(), std::string(), &error))
	{
		windows().note("skirmish-setup-failed", error);
		return;
	}
	restoreGame(m_strategic ? 1 : 0);
	refreshLobby();
}

// OpenBFME's own entries beside RotWK's record (not retail: RotWK's GameInfo text stays as retail writes it)
static const char *const kLegendaryKey = "OpenBFMELegendaryHeroes"; // the Legendary Heroes factor, 1 .. 4
static const char *const kHeroesKey = "OpenBFMEHeroes";             // per slot: "-" none, "random", or the hero's unique id; ',' between slots

void AptSkirmish::restoreExtensions(int mode)
{
	const std::string legendary = m_profiles->profileValue(mode, kLegendaryKey);
	if (!legendary.empty() && !m_setup->setLegendaryHeroes(std::atoi(legendary.c_str())))
	{
		windows().note("skirmish-kept-game", "the kept Legendary Heroes factor '" + legendary + "' is not offered: Off");
	}
	const std::string heroes = m_profiles->profileValue(mode, kHeroesKey);
	const CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	std::size_t at = 0;
	for (int slot = 0; slot < MAX_SLOTS && at < heroes.size(); ++slot)
	{
		const std::size_t end = heroes.find(',', at);
		const std::string id = heroes.substr(at, end == std::string::npos ? std::string::npos : end - at);
		at = end == std::string::npos ? heroes.size() : end + 1;
		if (id.empty() || id == "-" || !m_setup->info().customHeroesAllowed())
		{
			continue;
		}
		if (id == "random")
		{
			m_slotHero[(size_t)slot] = -2;
			continue;
		}
		const int index = list ? list->findByUniqueID(id) : -1;
		const CreateAHeroListEntry *e = list ? list->at(index) : nullptr;
		if (!e || !heroUsable(slot, e->hero))
		{
			windows().note("skirmish-kept-game", "slot " + std::to_string(slot) + ": the kept hero '" + id + "' is not in the hero list or not usable: none");
			continue;
		}
		if (applySlotHero(slot, &e->hero))
		{
			m_slotHero[(size_t)slot] = index;
		}
	}
}

std::string AptSkirmish::extensionHeroes() const
{
	const CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	std::string out;
	for (int slot = 0; slot < m_setup->info().serializedSlotCount(); ++slot)
	{
		const int v = m_slotHero[(size_t)slot];
		const CreateAHeroListEntry *e = list ? list->at(v) : nullptr;
		const std::string id = v == -2 ? "random" : (e && !e->hero.uniqueID.empty() && e->hero.uniqueID.find(',') == std::string::npos) ? e->hero.uniqueID : "-";
		out += (slot ? "," : "") + id;
	}
	return out;
}

void AptSkirmish::keepGame()
{
	// RW 0x821EBD (through RW 0x927FEF / 0x929440): the map ("Map", RW 0x821E55), the game (GameInfoToAsciiString(game, true), RW 0x821D55) and the hero
	// indexes, under the current profile, then the write
	if (m_lan || !m_setup || m_profiles->current().empty())
	{
		return;
	}
	const int mode = m_strategic ? 1 : 0;
	std::string map = m_setup->info().mapName;
	for (char &c : map)
	{
		c = c == '/' ? '\\' : c;
	}
	m_profiles->setProfileValue(mode, "Map", map);
	m_profiles->setProfileValue(mode, "GameInfo", SkirmishGameInfoToString(m_setup->info(), mode));
	m_profiles->setProfileValue(mode, "HeroIndexes", SkirmishHeroIndexesString());
	m_profiles->setProfileValue(mode, kLegendaryKey, std::to_string(m_setup->info().legendaryHeroes)); // lane UI-6 review: OpenBFME's own keys
	m_profiles->setProfileValue(mode, kHeroesKey, extensionHeroes());
	m_profiles->flush();
}

void AptSkirmish::runShutdown(bool *immediate)
{
	keepGame(); // lane UI-6: RW 0x927FEF, the screen's teardown slot
	AptScreen::runShutdown(immediate);
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
	const std::u16string name = GadgetListBoxGetText(m_profileList, selected, 0);
	const bool wasCurrent = name == m_profiles->current();
	m_profiles->remove(name);
	// lane UI-6 review: deleting the current profile drops its game with it (BFME2 0x43C2EB) and makes the first remaining profile current: the lobby is
	// set up again for that one (its kept game), so the screen's teardown keeps the survivor's own game, never the deleted one's
	if (wasCurrent && !m_profiles->current().empty())
	{
		reloadGameForProfile();
	}
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

void AptSkirmish::destroySlotScroll()
{
	if (AptGadgetLayer *layer = windows().gadgetLayer())
	{
		layer->destroyEngineGadget(m_slotScroll);
		layer->destroyEngineGadget(m_slotRange);
	}
	m_slotScroll = nullptr;
	m_slotRange = nullptr;
}

void AptSkirmish::setFirstVisibleSlot(int first)
{
	if (first < 0 || first > m_layoutCapacity - LEGACY_MAX_SLOTS)
	{
		windows().note("extended-layout-error", "slot viewport outside map capacity");
		return;
	}
	m_firstVisibleSlot = first;
	if (m_slotScroll)
	{
		GadgetSliderSetPosition(m_slotScroll, m_layoutCapacity - LEGACY_MAX_SLOTS - first);
	}
	if (m_slotRange)
	{
		int selected = -1;
		GadgetComboBoxGetSelectedPos(m_slotRange, &selected);
		if (selected != first)
		{
			GadgetComboBoxSetSelectedPos(m_slotRange, first, true);
		}
	}
}

void AptSkirmish::updateExtendedRows()
{
	pruneDeadGadgets();
	if (m_mapCapacityFilter) m_mapCapacityFilter->winHide(!m_mapList || m_mapList->winIsHidden());
	if (!m_setup)
	{
		return;
	}
	const int capacity = m_setup->info().mapStartCount;
	const bool extended = capacity > LEGACY_MAX_SLOTS;
	if (!extended && !m_extendedRowsParent)
	{
		return;
	}
	AptSpriteInst *parent = nullptr;
	for (const auto &component : windows().components())
	{
		if (component.window.get() != m_slots[0][(int)Leaf::Player] || !component.instance)
		{
			continue;
		}
		for (const AptCharacterInst *row = component.instance; row; row = row->parent())
		{
			if (row->instName() == "~0" && row->parent())
			{
				parent = row->parent()->asSprite();
				break;
			}
		}
		break;
	}
	if (!parent)
	{
		if (m_slotScroll) m_slotScroll->winHide(true);
		if (m_slotRange) m_slotRange->winHide(true);
		return; // the movie is entering or leaving its player page
	}
	if (capacity > MAX_SLOTS)
	{
		m_error = "extended layout: unsupported map capacity " + std::to_string(capacity);
		windows().note("extended-layout-error", m_error);
		return;
	}
	if (parent != m_extendedRowsParent)
	{
		for (int i = 0; i < LEGACY_MAX_SLOTS; ++i)
		{
			AptCharacterInst *row = parent->childByName("~" + std::to_string(i));
			if (!row) return; // the retail rows have not all been placed yet
			m_retailRowY[(size_t)i] = row->matrix.ty;
		}
		m_extendedRowsParent = parent;
		m_firstVisibleSlot = 0;
	}
	const int count = extended ? capacity : LEGACY_MAX_SLOTS;
	const bool capacityChanged = count != m_layoutCapacity;
	m_layoutCapacity = count;
	if (m_firstVisibleSlot > count - LEGACY_MAX_SLOTS) m_firstVisibleSlot = count - LEGACY_MAX_SLOTS;
	AptCharacterInst *prototype = parent->childByName("~0");
	// OpenBFME extension: instantiate the retail row symbol at runtime, not a replacement movie. Each row keeps its actual slot name, so its
	// InitGadgets and Ready callbacks retain their logical slot index while the viewport moves. No retail file or <=8-map layout is modified.
	for (int i = LEGACY_MAX_SLOTS; i < MAX_SLOTS; ++i)
	{
		const std::string name = "~" + std::to_string(i);
		AptCharacterInst *row = parent->childByName(name);
		if (i >= count)
		{
			if (row) parent->removeObject(row->depth());
			continue;
		}
		if (!row)
		{
			row = parent->attachCharacter(prototype->charRef(), name, 0x4000 + i, true);
			if (!row)
			{
				m_error = "extended layout: could not create player row " + std::to_string(i + 1);
				windows().note("extended-layout-error", m_error);
				return;
			}
			row->matrix = prototype->matrix;
			row->color = prototype->color;
		}
	}
	for (int i = 0; i < count; ++i)
	{
		AptCharacterInst *row = parent->childByName("~" + std::to_string(i));
		row->matrix.ty = extended ? m_retailRowY[0] + (i - m_firstVisibleSlot) * (m_retailRowY[1] - m_retailRowY[0]) : m_retailRowY[(size_t)i];
		row->asChanged = true;
		row->visible = i >= m_firstVisibleSlot && i < m_firstVisibleSlot + LEGACY_MAX_SLOTS;
	}
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (!layer) return;
	layer->syncPlacements();
	if (!extended)
	{
		destroySlotScroll();
		m_extendedRowsParent = nullptr;
		for (int i = 0; i < LEGACY_MAX_SLOTS; ++i)
		{
			if (GameWindow *team = m_slots[i][(int)Leaf::Team]) populateSlotGadget(i, Leaf::Team, team);
		}
		refreshAll();
		return;
	}
	if (!m_slotScroll && m_mapList)
	{
		GameWindow *skin = GadgetListBoxGetSlider(m_mapList);
		if (!skin)
		{
			m_error = "extended layout: map-list scrollbar skin is missing";
			windows().note("extended-layout-error", m_error);
			return;
		}
		WinInstanceData instance = *skin->winGetInstanceData();
		SliderData data = *static_cast<SliderData *>(skin->winGetUserData());
		data.minVal = 0;
		data.maxVal = count - LEGACY_MAX_SLOTS;
		m_slotScroll = layer->gadgets().gogoGadgetSlider(nullptr, WIN_STATUS_ENABLED | WIN_STATUS_APT_TEMPLATE, 890, 422, 18, 216, &instance, &data, skin->winGetFont(), false);
		std::string error;
		m_slotRange = layer->createEngineGadget("window/apt/combobox.wnd", 640, 650, 250, 24, ownerWindow(), &error);
		if (m_slotRange) GadgetComboBoxSetMaxDisplay(m_slotRange, 4);
		if (!m_slotScroll || !m_slotRange)
		{
			m_error = "extended layout: scrollbar creation failed: " + error;
			windows().note("extended-layout-error", m_error);
			destroySlotScroll();
			return;
		}
		m_slotScroll->winSetOwner(ownerWindow());
		m_slotScroll->winSetDrawFunc(skin->winGetDrawFunc());
		if (GameWindow *thumb = m_slotScroll->winGetChild())
		{
			*thumb->winGetInstanceData() = *skin->winGetChild()->winGetInstanceData();
			thumb->winSetOwner(m_slotScroll);
			thumb->winSetDrawFunc(skin->winGetChild()->winGetDrawFunc());
		}
	}
	if (m_slotRange && (capacityChanged || GadgetComboBoxGetLength(m_slotRange) != count - LEGACY_MAX_SLOTS + 1))
	{
		GadgetComboBoxReset(m_slotRange);
		for (int i = 0; i <= count - LEGACY_MAX_SLOTS; ++i)
		{
			const std::string label = "Players " + std::to_string(i + 1) + "-" + std::to_string(i + LEGACY_MAX_SLOTS) + " / " + std::to_string(count);
			GadgetComboBoxAddEntry(m_slotRange, std::u16string(label.begin(), label.end()), 0xFFFFFFFFu);
		}
		m_slotScroll->manager().winSendSystemMsg(m_slotScroll, GSM_SET_MIN_MAX, 0, (WindowMsgData)(count - LEGACY_MAX_SLOTS));
		setFirstVisibleSlot(m_firstVisibleSlot);
	}
	const bool shown = parent->globallyVisible() && !isHidden();
	if (m_slotScroll) m_slotScroll->winHide(!shown);
	if (m_slotRange) m_slotRange->winHide(!shown);
	if (capacityChanged)
	{
		for (int i = 0; i < count; ++i)
		{
			if (GameWindow *team = m_slots[i][(int)Leaf::Team]) populateSlotGadget(i, Leaf::Team, team);
		}
	}
}

void AptSkirmish::update()
{
	updateExtendedRows();
	if (m_statsPage)
	{
		m_statsPage->update(); // lane ONLINEHUB-1: the cards' text lines
	}
	// RotWK 0x928CDB
	serviceReadyBoxes(); // lane ONLINE-5
	if (m_ruleBox)
	{
		m_ruleBox->update(); // lane RULES-1: the rule warning box reaches its clip once GuiFX.apt is loaded (RW 0x9532F3)
	}
	switch (m_profileState)
	{
		case kStateInitialized:
			// ShowMain, state 5; with no current profile ShowAddProfile opens the add-profile popup (which sends OnNewProfileMenu)
			invokeRoot("ShowMain");
			m_profileState = kStateMain;
			if (m_profiles->current().empty())
			{
				invokeRoot("ShowAddProfile");
				if (m_env.fileSystem && isBfme1Data(*m_env.fileSystem))
					m_profileState = kStateNewProfile; // BFME1's PopUpPersona sends no OnNewProfileMenu: the popup is the new-profile page
			}
			break;
		case kStateStats:
			m_profileState = kStateStats + 1; // RotWK 0x928CDB: 8 -> 9
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
	if (m_env.fileSystem && isBfme1Data(*m_env.fileSystem))
	{
		registerScreenRef("MpGameSetup::GadgetInit", [this](const std::string &name, GameWindow *w) { initMpGameSetupGadget(name, w); }); // community PR #36: BFME1 MpGameSetupGadgetInit.cpp
	}
	registerScreenRef("MpGameSetup::InitGadgets", [this](const std::string &name, GameWindow *w) { initMpGameSetupGadget(name, w); });
	registerScreenRef("AptMapPreview::MapGadgetInit", [this](const std::string &name, GameWindow *w) { initMapPreviewGadget(name, w); });
	// lane UI-1: AptMpChat::InitGadgets (RW 0x987E4B): "Chat", "ChatPlayers", "ChatEntry"
	registerScreenRef("AptMpChat::InitGadgets", [this](const std::string &name, GameWindow *w) { initChatGadget(name, w); });
	// lane RULES-1: AptMpGameRules::InitGadgets (RW 0x987092): "RuleComboBox_<n>" / "RuleCheckBox_<n>"
	registerScreenRef("AptMpGameRules::InitGadgets", [this](const std::string &name, GameWindow *w) { initRuleGadget(name, w); });
	registerScreenRef("AptMpClans::InitGadgets", [this](const std::string &name, GameWindow *) {
		windows().note("unported-gadget-init", "AptMpClans::InitGadgets(" + name + ") [S-178]");
	});
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
	registerProvider("AptMpGameRules::ShowClans", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "0";
		}
		return true;
	});
	// lane RULES-1: AptMpGameRules::ExternFunc (RW 0x9865C9): query 0 "MpGameRules::NumComboBoxes" = RW 0x960A46(mode) (+ 1: OpenBFME's Legendary Heroes row
	// in mode 0), query 1 "MpGameRules::NumCheckBoxes" = RW 0x960A5D(mode), as "%d"
	registerProvider("MpGameRules::NumComboBoxes", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = std::to_string(ruleComboCount());
		}
		return true;
	});
	registerProvider("MpGameRules::NumCheckBoxes", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = std::to_string(ruleCheckCount()); // lane CHEAT-1: + 1 for OpenBFME's Cheat Mode row in mode 0
		}
		return true;
	});
	// ---- commands ----
	// RW 0x84xxxx (BFME2 0x43DC93, tier A): "RulesTab" refreshes the rule gadgets (the panel's 0x57E6D8 -> every rule's UpdateRuleGadget)
	registerCommand("MpGameSetup::OnTabSelect", [this](const std::string &argument) {
		m_activeTab = argument;
		if (argument == "RulesTab")
		{
			refreshRuleGadgets();
		}
	});
	registerCommand("MpGameSetup::OnReadyPress", [this](const std::string &argument) { onReadyPress(argument); });
	registerCommand("MpGameSetup::OnKickPlayer", [this](const std::string &argument) { onKickPlayer(argument); });
	registerCommand("AptMpChat::Send", [this](const std::string &) { sendChat(); }); // RW 0x987E3C: the argument is not read
	// RW 0x845BCC .. 0x845C80: the three sort commands call RW 0x84010E with 4 / 0 / 2
	registerCommand("MpGameSetup::OnSortIcons", [this](const std::string &) { setMapSort(4); });
	registerCommand("MpGameSetup::OnSortName", [this](const std::string &) { setMapSort(0); });
	registerCommand("MpGameSetup::OnSortPlayers", [this](const std::string &) { setMapSort(2); });
	registerCommand("AptMpGameRules::Reset", [this](const std::string &) { resetRules(); }); // lane RULES-1, RW 0x98674C
	for (const char *name : { "AptMpClans::WebSite", "AptMpClans::Delete" })
	{
		registerCommand(name, unportedCommand(name));
	}
	// ---- the map picture render component (a device texture) ----
	registerComponent("AptMapPreview::Picture", [this](AptComponentRequest &) -> std::shared_ptr<GameWindow> {
		windows().note("unported-component", "AptMapPreview::Picture: the map picture is a device texture [S-177]");
		return nullptr;
	});
}

void AptSkirmish::onReadyPress(const std::string &argument)
{
	// lane ONLINE-5: without an argument the movie's Ready boxes were just made (RW 0x84211D gives each "_none" here: the skirmish panel has no flag 0x20)
	if (argument.empty() || argument == "undefined")
	{
		readyBoxesMade();
		return;
	}
	windows().note("command-ignored", "MpGameSetup::OnReadyPress(" + argument + "): the skirmish lobby has no Ready boxes");
}

bool AptSkirmish::callSetupMovie(const std::string &function, const std::vector<std::string> &args)
{
	return level() >= 0 && windows().invokeAS(level(), function, args, nullptr, nullptr);
}

bool AptSkirmish::slotAccepted(int slot) const
{
	return m_setup && slot >= 0 && slot < MAX_SLOTS && m_setup->info().slots[slot].accepted;
}

bool AptSkirmish::slotGadgetsEnabled(int slot) const
{
	if (!lobbyEditable(slot))
	{
		return false;
	}
	// RW 0x843F6E (BFME2 0x441B15 UpdateSlot): the local slot, accepted, and the local player is not the host: every gadget off
	if (m_setup && m_setup->network() && slot == m_setup->localSlot() && !amIHost() && slotAccepted(slot))
	{
		return false;
	}
	return true;
}

void AptSkirmish::updateReadyIcon(int slot, bool force)
{
	// RW 0x84211D (see the header)
	if (!m_setup || slot < 0 || slot >= m_setup->info().mapStartCount)
	{
		return;
	}
	const SkirmishGameSlot &s = m_setup->info().slots[slot];
	const char *state = "_none"; // RW 0xC5416C
	if (setupFlags() & 0x20)
	{
		if (slot == 0)
		{
			state = "_disabledChecked"; // RW 0x84215C: slot 0 is the host's
		}
		else if (s.isOccupied())
		{
			if (s.isHuman())
			{
				const bool local = m_setup->localSlot() == slot; // the game's vslot 0x34 (RW 0x84217C)
				state = slotAccepted(slot) ? (local ? "_enabledChecked" : "_disabledChecked") : (local ? "_enabledUnchecked" : "_disabledUnchecked");
			}
			else
			{
				state = "_disabledChecked"; // an AI
			}
		}
	}
	if (!force && m_readyIcon[(size_t)slot] == state)
	{
		return;
	}
	// the movie's SetReadyState(index, to): `~<index>.Ready`.gotoAndPlay(to) (MpGameSetup.apt)
	if (callSetupMovie("SetReadyState", { std::to_string(slot), state }))
	{
		m_readyIcon[(size_t)slot] = state;
	}
	else
	{
		m_readyIcon[(size_t)slot].clear(); // the setup movie is not up (or has not defined the function yet): the update tries again
		m_readyDirty = true;
	}
}

void AptSkirmish::readyBoxesMade()
{
	// RW 0x841590's first branch only sets + 0x2B9: the slots are refreshed by the screen's next update
	m_readyDirty = true;
	for (std::string &shown : m_readyIcon)
	{
		shown.clear(); // new boxes show nothing yet
	}
}

void AptSkirmish::serviceChatLists()
{
	// lane ONLINE-5: the chat tab's lists are made while the tab's clip still plays in (the placeholder grows to its size over a few frames). A list
	// box filled at the small size scrolls to its last row and stays there: going MAP -> CHAT showed one line of the chat and one player. The lists
	// are filled again once their size has changed (INFERENCE: retail's gadgets are made at their final size by the movie's component, RW 0x814DDC)
	if (!m_chatList && !m_chatPlayers)
	{
		return;
	}
	pruneDeadGadgets();
	if (m_chatList)
	{
		int w = 0, h = 0;
		m_chatList->winGetSize(&w, &h);
		if (w != m_chatListW || h != m_chatListH)
		{
			m_chatListW = w;
			m_chatListH = h;
			GadgetListBoxReset(m_chatList);
			for (const auto &line : m_chatHistory)
			{
				GadgetListBoxAddEntryText(m_chatList, line.first, line.second, -1, 0);
			}
		}
	}
	if (m_chatPlayers)
	{
		int w = 0, h = 0;
		m_chatPlayers->winGetSize(&w, &h);
		if (w != m_chatPlayersW || h != m_chatPlayersH)
		{
			m_chatPlayersW = w;
			m_chatPlayersH = h;
			GadgetListBoxReset(m_chatPlayers);
			refreshChatPlayers();
		}
	}
}

void AptSkirmish::serviceReadyBoxes()
{
	serviceChatLists();
	if (!m_readyDirty || !m_setup)
	{
		return;
	}
	m_readyDirty = false;
	// P16: the movie's Ready boxes exist only for the game's slots (the retail setup movie has eight; an extended
	// lobby provides sixteen for a >8 map); never call SetReadyState for slots this game does not have
	for (int slot = 0; slot < m_setup->info().mapStartCount && slot < MAX_SLOTS; ++slot)
	{
		if (m_readyIcon[(size_t)slot].empty())
		{
			updateReadyIcon(slot, true); // a failure sets the flag again
		}
	}
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
		m_chatListW = m_chatListH = -1; // lane ONLINE-5: filled again at the list's final size (serviceChatLists)
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
		m_chatPlayersW = m_chatPlayersH = -1;
		GadgetListBoxReset(window);
		GadgetListBoxSetListLength(window, 1000);
		refreshChatPlayers(); // lane ONLINE-5
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

void AptSkirmish::refreshChatPlayers()
{
	if (!m_chatPlayers || !m_setup || !m_setup->network())
	{
		return;
	}
	std::vector<std::u16string> names;
	std::vector<PlatformKind> kinds; // lane PLATFORMICON-1: each player's system, shown as an icon before the name
	std::vector<UnicodeString> notes; // lane ONLINEMENU-1: the second column (the online room's ping)
	std::vector<Color> noteColors;
	bool anyNote = false;
	for (int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const SkirmishGameSlot &s = m_setup->info().slots[slot];
		if (s.isHuman())
		{
			names.push_back(s.name);
			kinds.push_back(s.platform);
			Color color = 0xFFFFFFFFu;
			notes.push_back(chatPlayerNote(slot, &color));
			noteColors.push_back(color);
			anyNote = anyNote || !notes.back().empty();
		}
	}
	// only when the room's players (or their notes) changed (a refill would drop the list's scroll position every poll)
	const int columns = anyNote ? 2 : 1;
	bool same = GadgetListBoxGetNumEntries(m_chatPlayers) == (int)names.size() && GadgetListBoxGetNumColumns(m_chatPlayers) == columns;
	for (int i = 0; same && i < (int)names.size(); ++i)
	{
		same = GadgetListBoxGetText(m_chatPlayers, i, 0) == names[(size_t)i] && GadgetListBoxGetCellBadge(m_chatPlayers, i, 0) == platformImageName(kinds[(size_t)i]);
		Color shown = 0;
		same = same && (columns == 1 || (GadgetListBoxGetTextAndColor(m_chatPlayers, &shown, i, 1) == notes[(size_t)i] && shown == noteColors[(size_t)i]));
	}
	if (same)
	{
		return;
	}
	// the widths in pixels follow the list's size now (it grows while the tab's clip plays in: serviceChatLists refills it then)
	if (columns == 2 || GadgetListBoxGetNumColumns(m_chatPlayers) != columns)
	{
		static const int kNameAndNote[2] = { 62, 38 };
		GadgetListBoxSetColumnWidths(m_chatPlayers, columns, columns == 2 ? kNameAndNote : nullptr);
	}
	if (MappedImageCollection *images = m_chatPlayers->manager().mutableImages())
	{
		registerPlatformIcons(*images);
	}
	GadgetListBoxReset(m_chatPlayers);
	for (std::size_t i = 0; i < names.size(); ++i)
	{
		const int row = GadgetListBoxAddEntryText(m_chatPlayers, names[i], 0xFFFFFFFFu, -1, 0);
		GadgetListBoxSetCellBadge(m_chatPlayers, row, 0, platformImageName(kinds[i]));
		if (columns == 2)
		{
			GadgetListBoxAddEntryText(m_chatPlayers, notes[i], noteColors[i], row, 1);
		}
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
	const bool wasStrategic = m_strategic;
	m_strategic = type == "Strategic"; // MpGameSetup+0x7C == 1
	if (m_setup)
	{
		m_setup->setStrategicMode(m_strategic);
		if (m_strategic && !wasStrategic && !m_lan)
		{
			restoreGame(1); // lane UI-6: the War of the Ring lobby's kept game (mode 1)
		}
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
	check(m_mapCapacityFilter);
	check(m_mapInfo);
	check(m_mapDescription);
	check(m_mapPicture);
	check(m_currentMap);
	check(m_profileList);
	check(m_profileEntry);
	check(m_chatList);
	check(m_chatPlayers);
	check(m_chatEntry);
	check(m_slotScroll);
	check(m_slotRange);
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
	pruneRuleGadgets(alive); // lane RULES-1
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
		if (m_currentMap != window)
		{
			m_currentMap = window;
			m_currentMapChildren.clear();
			for (GameWindow *child = window->winGetChild(); child && m_currentMapChildren.size() < LEGACY_MAX_SLOTS; child = child->winGetNext())
			{
				m_currentMapChildren.push_back(child);
				child->winHide(true);
				child->winSetStatus(WIN_STATUS_RIGHT_CLICK); // RW 0x9778B0: status | 0x20000
			}
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
			GadgetComboBoxSetMaxDisplay(combo, std::max(8 - slot, 4)); // RW 0x844F3C -> 0x72414F, argument RW 0x8402C1
			break;
		case Leaf::Team:
			fillCombo(combo, m_setup->teamEntries());
			break;
		case Leaf::Color:
			fillImageCombo(combo, m_setup->colorEntries(slot));
			break;
		case Leaf::Handicap:
			// lane OWNERFIX-2: RW 0x840B9D (BFME2 decomp AptMpGameSetup::rva0043E253, tier A): 0% .. -95%, the first entry selected
			if (handicapChoiceAvailable())
			{
				fillCombo(combo, m_setup->handicapEntries());
				GadgetComboBoxSetSelectedPos(combo, 0, false);
			}
			GadgetComboBoxSetMaxDisplay(combo, std::max(8 - slot, 4)); // RW 0x840C40 -> 0x72414F
			break;
		case Leaf::Hero:
			fillHeroCombo(slot, combo); // lane CAH-1
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

void AptSkirmish::createMapCapacityFilter()
{
	if (m_mapCapacityFilter || !m_setup) return;
	const auto &maps = m_setup->source().maps();
	if (std::none_of(maps.begin(), maps.end(), [](const MapCacheEntry &map) {
		return map.isMultiplayer && map.numPlayers > LEGACY_MAX_SLOTS && map.numPlayers <= MAX_SLOTS;
	})) return;
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (!layer) return;
	std::string error;
	m_mapCapacityFilter = layer->createEngineGadget("window/apt/combobox.wnd", 430, 168, 300, 24, ownerWindow(), &error);
	if (!m_mapCapacityFilter)
	{
		m_error = "map capacity filter: " + error;
		windows().note("extended-layout-error", m_error);
		return;
	}
	GadgetComboBoxAddEntry(m_mapCapacityFilter, u"Maps: all player counts", 0xFFFFFFFFu);
	GadgetComboBoxAddEntry(m_mapCapacityFilter, u"Maps: 9-16 players", 0xFFFFFFFFu);
	GadgetComboBoxSetSelectedPos(m_mapCapacityFilter, m_largeMapsOnly ? 1 : 0, true);
}

void AptSkirmish::populateMapList()
{
	// MpGameSetup's map list fill (RW 0x8460B5; the column widths {8, 2, 70, 10, 10} come from MpGameSetup::InitGadgets RW 0x840906):
	//   * every map gets a conquest state (+0xF4): 0 without the battle honors (RotWK reads them only with flag +0x3A4 bit 0) or for a map that is not
	//     multiplayer; lane OWNERFIX-3: with the flag (the skirmish screen's 0x10000007) the current profile's RealTimeStats.ini decides 1 .. 6
	//     (GUI/Skirmish/RealTimeStats.h); a map that is not official (+0x26 == 0) has 0x8000 added;
	//   * the maps are sorted by RW 0x84606C with the comparator RW 0x84246C: the primary key +0x3AC (2 at construction, RW 0x8451A9), then the
	//     secondary +0x3B0 (0), then name, player count, state and finally the record address (here: the cache order);
	//   * per map, RW 0x846674 ..: the state's image (RealTimeStatsPreferences::conquestImage) in column 0, width = height = min(AptDifficultyHardConquered's width (10 without it), column 0's width); the
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
	m_mapStates.clear();
	m_mapImages.clear();
	createMapCapacityFilter();
	const std::vector<MapCacheEntry> &maps = m_setup->source().maps();
	// lane OWNERFIX-3: RW 0x8460B5 with the flag bit 0: the current profile's stats (SkirmishPreferences RW 0x821A14 -> RealTimeStatsPreferences RW 0x93E72F)
	RealTimeStatsPreferences statsFile;
	const RealTimeStatsPreferences *stats = nullptr;
	if (setupFlags() & 1)
	{
		if (!m_env.profiles)
		{
			windows().note("map-honors", "[S-5041] the map list's battle honors: the host gave no Skirmish profiles; no map shows a conquest");
		}
		else
		{
			std::string error;
			if (!statsFile.load(m_env.profiles->folder(), m_env.profiles->current(), &error))
			{
				windows().note("map-honors", "[S-5041] the map list's battle honors: " + error);
			}
			stats = &statsFile; // a file that could not be read gives no counters (retail: an empty map), and the note says why
		}
	}
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
		if (m_largeMapsOnly && (m.numPlayers <= LEGACY_MAX_SLOTS || m.numPlayers > MAX_SLOTS)) continue;
		const int state = RealTimeStatsPreferences::conquestState(stats, m);
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
		const char *image = RealTimeStatsPreferences::conquestImage(r.state);
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
		m_mapStates.push_back(r.state);
		m_mapImages.push_back(image);
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
	updateReadyIcon(slot); // lane ONLINE-5: RW 0x843F6E (BFME2 0x441B15 UpdateSlot) begins with the slot's Ready box
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
					w->winEnable(slotGadgetsEnabled(slot));
				}
			}
		}
		updatePlatformBadge(slot, combo);
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
	if (GameWindow *combo = m_slots[slot][(int)Leaf::Handicap])
	{
		// lane OWNERFIX-2: the entry whose item data is the slot's + 0x20 (BFME2 decomp MpGameSetupSlots.cpp UpdateSlot, the handicap block); a combo that
		// still has no entries (a lobby that never populated it) is shown locked, never as a drop-down that opens on nothing (lane ONLINE-5)
		if (GadgetComboBoxGetLength(combo) == 0)
		{
			combo->winEnable(false);
		}
		else
		{
			selectByItemData(combo, s.handicap);
		}
	}
	if (GameWindow *combo = m_slots[slot][(int)Leaf::Hero]) // lane CAH-1: the slot's choice (RW 0x72437D)
	{
		// lane RULES-1: RW 0x842A85: the hero combo is enabled only while AllowCustomHeroes (rule 0) is 1; otherwise disabled with its choice dropped
		// (SkirmishGameSetup::setRule cleared the slots' records; the combo's own choice goes back to "-")
		if (m_setup->info().rules[RULE_ALLOW_CUSTOM_HEROES] != 1 && !MpGameRules::isNone(m_setup->info().rules))
		{
			m_slotHero[(size_t)slot] = -1;
			combo->winEnable(false);
		}
		else if (!heroChoiceAvailable())
		{
			combo->winEnable(false); // lane ONLINE-5: shown locked where a lobby carries no Create-a-Hero record
		}
		else if (!m_setup->network() || slotGadgetsEnabled(slot))
		{
			combo->winEnable(true);
		}
		if (slot == m_setup->localSlot())
		{
			selectByItemData(combo, m_slotHero[(size_t)slot]);
		}
		else if (m_setup->network())
		{
			fillHeroCombo(slot, combo); // another player's choice, as text (RW 0x841A9E)
			if (GadgetComboBoxGetLength(combo) > 0)
			{
				GadgetComboBoxSetSelectedPos(combo, 0, true);
			}
		}
	}
	m_refreshing = outer;
}

void AptSkirmish::updatePlatformBadge(int slot, GameWindow *nameWindow)
{
	// lane PLATFORMICON-1 (OpenBFME): the system the slot's human player told (LAN: PLATFORM_INFO, online: the lobby's slot `platform`), as an icon just
	// left of the name. An AI, an open or closed slot, a player that told nothing (an older build), and single player show none
	const SkirmishGameSlot &s = m_setup->info().slots[slot];
	PlatformKind kind = m_setup->network() && s.isHuman() ? s.platform : PlatformKind::None;
	if (kind != PlatformKind::None)
	{
		if (MappedImageCollection *images = nameWindow->manager().mutableImages())
		{
			registerPlatformIcons(*images);
		}
		nameWindow->winSetBadge(platformImageName(kind), kPlatformIconStage, platformHelpText(kind));
		if (!m_platformNoted)
		{
			m_platformNoted = true;
			windows().note("platform-icon", "the staging room shows the players' systems as icons; no hover text (the menus have no help box) [S-4850]");
		}
	}
	else
	{
		nameWindow->winSetBadge(std::string(), 0, std::string());
	}
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
	refreshRuleGadgets(); // lane RULES-1: a network game's rules come with the host's options
	refreshChatPlayers(); // lane ONLINE-5
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
	updateMapStartSpots(); // lane FB7-1: the spots' labels for the slots' positions (RW 0x7052BE)
	if (m_mapInfo)
	{
		GadgetListBoxReset(m_mapInfo);
		if (map)
		{
			const std::u16string text = m_error.compare(0, 12, "map preview:") == 0 ? std::u16string(m_error.begin(), m_error.end()) : m_setup->mapDescription(*map);
			GadgetListBoxAddEntryText(m_mapInfo, text, 0xFFFFFFFFu, -1, -1, true);
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
	// Map art needs a vertical flip after the texture loader normalizes TGA storage order.
	if (m_setup->source().fileExists(base + "_art.tga"))
	{
		image.filename = base + "_art.tga";
		image.uvLo[0] = 0.0f;
		image.uvLo[1] = 1.0f;
		image.uvHi[0] = 1.0f;
		image.uvHi[1] = 0.0f;
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
	if (m_error.compare(0, 12, "map preview:") == 0)
	{
		m_error.clear();
	}
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
	// OpenBFME extended maps reuse the retail start button's skin and input, owned by the same map window.
	if (map && map->numPlayers > LEGACY_MAX_SLOTS && !m_currentMapChildren.empty())
	{
		GameWindow *prototype = m_currentMapChildren.front();
		int w = 0, h = 0;
		prototype->winGetSize(&w, &h);
		while (m_currentMapChildren.size() < MAX_SLOTS)
		{
			WinInstanceData instance = *prototype->winGetInstanceData();
			GameWindow *button = window->manager().gogoGadgetPushButton(window, prototype->winGetStatus(), 0, 0, w, h, &instance, prototype->winGetFont(), false);
			if (!button)
			{
				break;
			}
			m_currentMapChildren.push_back(button);
		}
	}
	// RW 0x976F59: the start spots. Validate the whole preview before showing any marker.
	GameWindow *buttons[MAX_SLOTS] = {};
	for (std::size_t i = 0; i < m_currentMapChildren.size() && i < MAX_SLOTS; ++i)
	{
		buttons[i] = m_currentMapChildren[i];
	}
	// In a sparse map cache entry a previously displayed button may no longer have a start
	// position. Hide every button before showing the positions this map actually provides.
	for (GameWindow *button : buttons)
	{
		if (button)
		{
			button->winHide(true);
		}
	}
	if (map)
	{
		std::string error;
		if (map->numPlayers > MAX_SLOTS)
		{
			error = "map preview: unsupported player capacity " + std::to_string(map->numPlayers);
		}
		for (int i = 0; error.empty() && i < map->numPlayers; ++i)
		{
			if (!map->startPositions.count(i + 1) || !buttons[i])
			{
				error = "map preview: missing start position or control " + std::to_string(i + 1);
			}
		}
		if (!error.empty())
		{
			m_error = error;
			windows().note("map-preview-error", error);
			window->winEnable(false);
			for (GameWindow *button : buttons)
			{
				if (button)
				{
					button->winHide(true);
				}
			}
			return;
		}
	}
	int shown = 0;
	if (map)
	{
		window->winEnable(true);
		// RW 0x976F9C .. 0x976FB6: only for a multiplayer map (MapMetaData + 0x24) while TheGameLogic + 0x114 is 3. Lane ONLINE-5: + 0x114 is the battle
		// kind, and 3 is "not a War of the Ring battle" (docs/SAVES-RETAIL.md: 0 a single-player War of the Ring battle, 1 / 2 a multiplayer one): the
		// value GameLogic's constructor sets (RW 0x6301D3) and the logic dispatcher sets again when a game's data is cleared (RW 0x779F20). So every
		// ordinary lobby that hosts the setup movie shows the map's start spots: the skirmish, the LAN and the online staging room alike. (FB7-1 took 3
		// for "a skirmish" and hid them in the network lobbies: the owner's "no spot to place where you wanted to play".) NOT PORTED: a War of the
		// Ring battle's lobby (kinds 0 .. 2) would hide them; this port's War of the Ring does not use this screen for its battles
		if (map->isMultiplayer && map->numPlayers > 0)
		{
			for (; shown < map->numPlayers && shown < MAX_SLOTS; ++shown)
			{
				auto spot = map->startPositions.find(shown + 1); // "Player_%d_Start"
				GameWindow *button = buttons[shown];
				MapPreviewPositionStartSpot(button, window, spot->second, *map, buttons, MAX_SLOTS);
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
	for (int i = shown; i < MAX_SLOTS; ++i)
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
	if (gadget && gadget == m_mapCapacityFilter && msg == GCM_SELECTED)
	{
		if (m_refreshing) return MSG_HANDLED;
		int selected = -1;
		GadgetComboBoxGetSelectedPos(gadget, &selected);
		if (selected != 0 && selected != 1) return MSG_HANDLED;
		m_largeMapsOnly = selected == 1;
		populateMapList();
		return MSG_HANDLED;
	}
	if (gadget && gadget == m_slotScroll && (msg == GSM_SLIDER_TRACK || msg == GSM_SLIDER_DONE))
	{
		setFirstVisibleSlot(m_layoutCapacity - LEGACY_MAX_SLOTS - GadgetSliderGetPosition(gadget));
		return MSG_HANDLED;
	}
	if (gadget && gadget == m_slotRange && msg == GCM_SELECTED)
	{
		int selected = -1;
		GadgetComboBoxGetSelectedPos(gadget, &selected);
		setFirstVisibleSlot(selected);
		return MSG_HANDLED;
	}
	switch (msg)
	{
		case GCM_SELECTED:
			if (onRuleGadget(gadget)) // lane RULES-1, RW 0x986776
			{
				return MSG_HANDLED;
			}
			onComboSelected(gadget);
			return MSG_HANDLED;
		case GBM_SELECTED:
			if (onRuleGadget(gadget)) // lane RULES-1: a rule check box
			{
				return MSG_HANDLED;
			}
			[[fallthrough]];
		case GBM_SELECTED_RIGHT:
			// lane FB7-1: the map window's start spots forward their clicks to the screen (PassSelectedButtonsToParentSystem, RW 0x6C1501)
			return onStartSpotSelected(gadget, msg == GBM_SELECTED) ? MSG_HANDLED : MSG_IGNORED;
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

int AptSkirmish::nextSelectableStartSlot(int from) const
{
	// RW 0x975C3A (ZH getNextSelectablePlayer): the host takes the first slot from `from` with no start position, not an observer (+0x18 != -2), that
	// is the local slot or an AI; a joiner only its own slot, when it has no position
	const SkirmishGameInfo &info = m_setup->info();
	const int local = m_setup->localSlot();
	if (!lobbyEditable(-1))
	{
		return info.slots[local].startPos == -1 ? local : -1;
	}
	for (int i = from; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &s = info.slots[i];
		if (s.startPos == -1 && s.playerTemplate != PLAYERTEMPLATE_OBSERVER && (i == local || s.isAI()))
		{
			return i;
		}
	}
	return -1;
}

bool AptSkirmish::onStartSpotSelected(GameWindow *button, bool left)
{
	// RW 0x845830 (messages 0x4008 / 0x4009; ZH SkirmishGameOptionsMenu's buttonMapStartPosition branch)
	if (!m_setup || !button)
	{
		return false;
	}
	int position = -1; // RW 0x975782: the button's index among the map window's eight
	for (std::size_t i = 0; i < m_currentMapChildren.size() && i < MAX_SLOTS; ++i)
	{
		if (m_currentMapChildren[i] == button)
		{
			position = (int)i;
		}
	}
	if (position < 0)
	{
		return false;
	}
	const SkirmishGameInfo &info = m_setup->info();
	const int local = m_setup->localSlot();
	bool changed = false;
	int holder = -1;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (info.slots[i].startPos == position)
		{
			holder = i;
			break;
		}
	}
	if (holder >= 0)
	{
		// a taken spot: only its own player, or the host for an AI, moves it: a left click passes it to the next selectable slot after the holder,
		// a right click frees it
		if (holder != local && (!lobbyEditable(-1) || !info.slots[holder].isAI()))
		{
			return true;
		}
		const int next = left ? nextSelectableStartSlot(holder + 1) : -1;
		changed |= m_setup->setSlotStartPos(holder, -1); // RW 0x8404F2(holder, -1)
		if (next >= 0)
		{
			changed |= m_setup->setSlotStartPos(next, position);
		}
	}
	else
	{
		// a free spot (either button): the first selectable slot takes it
		const int next = nextSelectableStartSlot(0);
		if (next >= 0)
		{
			changed |= m_setup->setSlotStartPos(next, position);
		}
	}
	if (changed)
	{
		updateMapStartSpots(); // RW 0x8404F2 sets +0x2BE; the update (RW 0x846BA4 .. 0x846BB9) redraws the spots
		lobbyChanged();        // lane ONLINE-5: RW 0x8404F2 ends in the owner's vslot 2: a network lobby sends the change on (the host, the lobby service)
	}
	return true;
}

void AptSkirmish::updateMapStartSpots()
{
	// RW 0x7052BE (ZH updateMapStartSpots, onLoadScreen false): every spot blank with the tooltip TOOLTIP:StartPosition; then each slot whose start
	// position is below the map's player count and that is not an observer labels its spot NUMBER:<slot + 1> with TOOLTIP:StartPositionN; a position
	// at or past the player count is reset to -1. Without the map in the cache every spot is GUI:Blank (RW 0x7052F5).
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
	GameWindow *buttons[MAX_SLOTS] = {};
	for (std::size_t i = 0; i < m_currentMapChildren.size() && i < MAX_SLOTS; ++i)
	{
		buttons[i] = m_currentMapChildren[i];
	}
	if (!map)
	{
		for (GameWindow *b : buttons)
		{
			if (b)
			{
				GadgetButtonSetText(b, fetchOrMissing(m_env.gameText, "GUI:Blank"));
			}
		}
		return;
	}
	for (GameWindow *b : buttons)
	{
		if (b)
		{
			GadgetButtonSetText(b, std::u16string());
			b->winSetTooltip(fetchOrMissing(m_env.gameText, "TOOLTIP:StartPosition"));
		}
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &s = m_setup->info().slots[i];
		const int pos = s.startPos;
		if (pos >= map->numPlayers)
		{
			m_setup->setSlotStartPos(i, -1);
			continue;
		}
		if (pos < 0 || s.playerTemplate <= PLAYERTEMPLATE_OBSERVER || pos >= MAX_SLOTS || !buttons[pos])
		{
			continue;
		}
		GadgetButtonSetText(buttons[pos], fetchOrMissing(m_env.gameText, "NUMBER:" + std::to_string(i + 1)));
		// TOOLTIP:StartPositionN is a format with the slot's number (RW 0x7054B0: fetch with the number as the argument)
		std::u16string tip = fetchOrMissing(m_env.gameText, "TOOLTIP:StartPositionN");
		const std::size_t at = tip.find(u"%d");
		if (at != std::u16string::npos)
		{
			const std::string n = std::to_string(i + 1);
			tip.replace(at, 2, std::u16string(n.begin(), n.end()));
		}
		buttons[pos]->winSetTooltip(tip);
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
			m_setup->setSlotHandicap(slot, value); // lane OWNERFIX-2: RW 0x840485 -> the owner's MpOwnerSelectHandicap (vslot 5)
			break;
		case Leaf::Hero:
			onHeroSelected(slot, value); // lane CAH-1
			break;
	}
	if (it->second.leaf == Leaf::PlayerTemplate && m_slotHero[(size_t)slot] >= 0 && m_env.createAHero && m_env.createAHero->heroes)
	{
		// lane CAH-1 (BFME2 0x44149C after a faction change): a hero the new side cannot use is dropped (INFERENCE, S-1406: retail puts the side's
		// default hero in, RW's GetDefaultHero was not read)
		const CreateAHeroListEntry *e = m_env.createAHero->heroes->at(m_slotHero[(size_t)slot]);
		if (!e || !heroUsable(slot, e->hero))
		{
			onHeroSelected(slot, -1);
		}
	}
	if (GameWindow *heroCombo = m_slots[slot][(int)Leaf::Hero]) // the entries follow the side (RW 0x842C93 greys the heroes the side cannot use)
	{
		if (it->second.leaf == Leaf::PlayerTemplate)
		{
			m_refreshing = true;
			fillHeroCombo(slot, heroCombo);
			m_refreshing = false;
		}
	}
	refreshAll();
	lobbyChanged();
}

// ---- lane CAH-1: the Hero combo ------------------------------------------------------------------------------------------------------------------------------
// RW 0x842C93 (MpGameSetup's hero combo of a slot, + 0x374): "-" (item data -1), GUI:Random (-2), then for the local slot (or an AI this host runs) every hero of
// the list from the last to the first (item data = its index), a system hero's name followed by VALUE:Default, greyed when the slot's side is not one of the
// hero subclass's UsableFactions (RW 0x619CE6); the slot's choice (+ 0x5C) is selected. Another player's slot shows its choice's text (RW 0x841A9E): "-",
// GUI:Random, a system hero's name + VALUE:Default, or the subclass's name (RW 0x61AE43).
// INFERENCE (S-1406): AI slots get no hero choice here (the host-run AI test of RW 0x842D... was not followed); the disabled entry is a grey text.

const char *const AptSkirmish::kHeroComboStop =
	"[S-1406] lobby Hero combo: the side of a faction is its Side name among the Create-a-Hero faction names, AI slots get no hero, a hero the new faction "
	"cannot use is dropped (retail's default hero of the side, GetDefaultHero, not read), a disabled entry is grey text, and GUI:Random is not resolved at the "
	"game start";

int AptSkirmish::heroSide(int slot) const
{
	if (!m_setup || slot < 0 || slot >= MAX_SLOTS)
	{
		return -1;
	}
	const SkirmishFaction *f = m_setup->faction(m_setup->info().slots[slot].playerTemplate); // RW 0x5FCAD9 / 0x5FC95B
	if (!f)
	{
		return -1;
	}
	for (int i = 0; i < 9; ++i) // TheCreateAHeroFactionNames (RW 0xD9EDD0): Men .. Neutral; INFERENCE (S-1406): the template's Side name picks the index
	{
		if (AsciiStringUtil::compareNoCase(f->side, TheCreateAHeroFactionNames[i]) == 0)
		{
			return i;
		}
	}
	return 8;
}

bool AptSkirmish::heroUsable(int slot, const CreateAHeroHero &hero) const
{
	const int side = heroSide(slot);
	if (side < 0)
	{
		return true;
	}
	const CreateAHeroSystem *sys = m_env.createAHero ? m_env.createAHero->system : nullptr;
	const CreateAHeroSubClass *sub = sys ? sys->subClass(hero.classIndex, hero.subClassIndex) : nullptr;
	return sub && std::find(sub->usableFactions.begin(), sub->usableFactions.end(), side) != sub->usableFactions.end();
}

void AptSkirmish::fillHeroCombo(int slot, GameWindow *combo)
{
	GadgetComboBoxReset(combo);
	const Color white = 0xFFFFFFFFu;
	const Color grey = 0xFF808080u;
	const CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	if (!list || !m_setup)
	{
		return; // without TheCreateAHeroSystem RW 0x842C93 leaves the combo empty
	}
	GadgetComboBoxSetMaxDisplay(combo, std::max(8 - slot, 4)); // RW 0x842F92 -> 0x72414F; independent of the number of heroes
	if (!m_heroStopReported)
	{
		m_heroStopReported = true;
		windows().note("create-a-hero-stop", kHeroComboStop);
	}
	const SkirmishGameSlot &s = m_setup->info().slots[slot];
	const bool local = slot == m_setup->localSlot();
	auto add = [&](const std::u16string &text, int data, Color c) {
		const int at = GadgetComboBoxAddEntry(combo, text, c);
		GadgetComboBoxSetItemData(combo, at, itemData(data));
	};
	if (!local)
	{
		// RW 0x841A9E: the slot's choice as text
		std::u16string text = u"-";
		if (m_slotHero[(size_t)slot] == -2)
		{
			text = fetchOrMissing(m_env.gameText, "GUI:Random");
		}
		else if (s.hasCreateAHero)
		{
			if (s.createAHero.isSystemHero)
			{
				text = s.createAHero.name + fetchOrMissing(m_env.gameText, "VALUE:Default");
			}
			else
			{
				const CreateAHeroSubClass *sub = m_env.createAHero->system ? m_env.createAHero->system->subClass(s.createAHero.classIndex, s.createAHero.subClassIndex) : nullptr;
				text = sub ? fetchOrMissing(m_env.gameText, sub->nameTag) : s.createAHero.name;
			}
		}
		add(text, m_slotHero[(size_t)slot], white);
		return;
	}
	add(u"-", -1, white);
	add(fetchOrMissing(m_env.gameText, "GUI:Random"), -2, white);
	for (int i = (int)list->size() - 1; i >= 0; --i)
	{
		const CreateAHeroHero &h = list->at(i)->hero;
		std::u16string text = h.name;
		if (h.isSystemHero)
		{
			text += fetchOrMissing(m_env.gameText, "VALUE:Default");
		}
		add(text, i, heroUsable(slot, h) ? white : grey);
	}
}

void AptSkirmish::onHeroSelected(int slot, int value)
{
	// BFME2 0x43DD34: a listed hero, random (-2) or nothing (-1); a hero the side cannot use is refused (its entry is disabled in retail)
	const CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	const CreateAHeroListEntry *e = list ? list->at(value) : nullptr;
	if (e && !heroUsable(slot, e->hero))
	{
		return;
	}
	if (!e && value != -2)
	{
		value = -1;
	}
	if (value == -2)
	{
		windows().note("create-a-hero-lobby", "slot " + std::to_string(slot) + ": GUI:Random hero chosen; the game start does not resolve a random hero [S-1406]");
	}
	m_slotHero[(size_t)slot] = value;
	applySlotHero(slot, e ? &e->hero : nullptr);
}

bool AptSkirmish::applySlotHero(int slot, const CreateAHeroHero *hero)
{
	std::string error;
	if (!m_setup || !m_setup->setSlotCreateAHero(slot, hero, &error))
	{
		windows().note("create-a-hero-lobby", "slot " + std::to_string(slot) + ": the hero was refused: " + error);
		return false;
	}
	return true;
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
	if (m_env.fileSystem && isBfme1Data(*m_env.fileSystem))
	{
		keepGame(); // community PR #36, BFME1 data only: the chosen map is remembered at once (docs/BFME1.md)
	}
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
	// lane GAMEMODES-1: RotWK's start check (RW 0x8431B8, SkirmishGameSetup::checkStart). The skirmish owner's vslot 0x44 (RW 0x928482) shows only the lines of
	// kind 1, as MessageBoxOk(L"", text) (RW 0x84ED5D); "Entering sandbox mode..." (kind 0) and the lines of kind 2 show nothing in a skirmish
	const SkirmishGameSetup::StartCheck check = m_setup->checkStart();
	for (const std::string &stop : check.stops)
	{
		windows().note("unported", stop);
	}
	for (const SkirmishGameSetup::StartCheck::Line &line : check.lines)
	{
		if (line.kind == 1)
		{
			m_env.newGame->startRefused(std::u16string(), line.text);
		}
	}
	if (!check.ok)
	{
		windows().note("start-refused", "AptSkirmish::StartGame: " + (check.refusalLabel.empty() ? std::string("the map gives no player count (RW 0x843263)") : check.refusalLabel));
		return false;
	}
	if (check.sandbox)
	{
		windows().note("sandbox", "AptSkirmish::StartGame: one team: a sandbox game (GUI:SandboxMode, RW 0x8436D8; the skirmish owner shows no kind 0 line)");
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
	// lane OWNERFIX-3: the game the score screen records the battle honors of (retail: TheGameInfo, read by RW 0x9CE8F3 at the score screen); a lobby
	// without the honors flag (LAN, online) leaves none
	if (m_env.skirmishHonors)
	{
		SkirmishHonorsGame &h = *m_env.skirmishHonors;
		h = SkirmishHonorsGame();
		if ((setupFlags() & 1) && !message.game.cheatModeOn()) // lane CHEAT-1: a Cheat Mode game earns no battle honors
		{
			h.valid = true;
			for (int i = 0; i < MAX_SLOTS; ++i)
			{
				h.slots[(size_t)i].state = (int)message.game.slots[i].state;
				h.slots[(size_t)i].team = message.game.slots[i].teamNumber;
			}
			h.localSlot = m_setup->localSlot();
			h.mapKey = message.game.mapName;
			for (const MapCacheEntry &m : m_setup->source().maps())
			{
				h.mapPlayers = m.name == h.mapKey ? m.numPlayers : h.mapPlayers; // GameInfo RW 0x803C52: the cache's player count, -1 not there
			}
		}
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
