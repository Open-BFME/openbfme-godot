// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptSimpleScreens.h.

#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/PlayerStatusInfo.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/OptionPreferences.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"

#include "GameClient/GUI/GameTextSource.h"

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

AptGuiFXScreen::AptGuiFXScreen(WindowManager &windows, Shell &shell) : AptScreen(windows, shell, "GuiFX.apt", "AptGuiFX", 11)
{
	registerCommand("AptGuiFX::OnInitialized", unportedCommand("AptGuiFX::OnInitialized"));
	registerComponent("ToolTipText", [this](AptComponentRequest &) -> std::shared_ptr<GameWindow> {
		this->windows().note("unported-component", "ToolTipText [S-175]");
		return nullptr;
	});
}

// lane PLAY-1: PlayerTribute.apt (see the header)
AptPlayerTribute::AptPlayerTribute(WindowManager &windows, Shell &shell, ShellServices &services, ShellEnvironment &environment)
	: AptScreen(windows, shell, "PlayerTribute.apt", "AptPlayerTribute"), m_services(services), m_env(environment)
{
	if (level() < 0)
	{
		return;
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
	for (const char *name : { "_Send", "_Reset" })
	{
		registerCommand(p + name, unportedCommand(p + name + " [S-1922]"));
	}
	registerCommand("AptPlayerTribute::OnInitialized", unportedCommand("AptPlayerTribute::OnInitialized [S-1922]"));
	// RW 0x91774D: the tribute page is offered when this extern is true; MSG_GIVE_MONEY has no logic port, so the movie keeps to its status page (S-1922)
	registerProvider(p + "_TributeEnabled", [this](const std::string &name, std::string &value, bool setting) {
		if (setting)
		{
			return true;
		}
		this->windows().note("provider-unported", name + ": false (the tribute is not ported) [S-1922]");
		value.clear();
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
		windows().note("unported-command", "TributePage " + name + " [S-1922]");
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

const char *AptPlayerTribute::stopLine()
{
	return "[S-1922] PlayerTribute.apt (the Palantir's flag in a skirmish / multiplayer game, RW 0x6D40C9 -> RW 0x914EF0): the screen opens over the game and closes "
		   "(its Cancel / Escape: <path>_ReturnToGame, RW 0x914C51); not ported: the tribute (MSG_GIVE_MONEY has no logic port, so <path>_TributeEnabled answers "
		   "false and the movie shows its status page), the opening's pause / input calls (RW 0x914FA2 .. 0x914FE2), the campaign's objectives screen "
		   "(RW 0x8E8843, AptObjectivesMenu)";
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
// RW 0xD9E760: GameLODManager's audio LOD names (index 0, 1)
const char *const kAudioLOD[2] = { "Low", "High" };
// the build's version (RW 0x644CC2 parses "VERSION=2.1.2614.37001" of the build string RW 0x644D8A with "%d.%d.%d.%d"; major + 0, minor + 4)
constexpr int kVersionMajor = 2, kVersionMinor = 1;

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

AptOptionsScreen::AptOptionsScreen(WindowManager &windows, Shell &shell, ShellEnvironment &environment, ShellServices &services)
	: AptScreen(windows, shell, "Options.apt", "AptOptions"), m_env(environment), m_services(services)
{
	m_pendingSoft = m_env.options ? m_env.options->softParticles() : true;
	if (!m_env.options)
	{
		windows.note("options-unwired", "Options: no OptionPreferences: Save writes nothing [S-1484]");
	}
	// RW 0x816157 (the shared OnInitialized): the screen's state + 0x27C becomes 1, the basic page
	registerCommand("AptOptions::OnInitialized", [this](const std::string &) { m_state = 1; });
	registerCommand("AptOptions::Cancel", [this](const std::string &) { cancel(); });
	registerCommand("AptOptions::Save", [this](const std::string &) { save(); });
	registerCommand("AptOptions::Reset", [this](const std::string &) { reset(); });
	registerCommand("AptOptions::RefreshNat", unportedCommand("AptOptions::RefreshNat"));
	registerCommand("AptOptions::EnterAdvancedSettings", unportedCommand("AptOptions::EnterAdvancedSettings"));
	registerScreenRef("AptOptions::InitGadgets", [this](const std::string &name, GameWindow *w) { initGadget(name, w); });
	for (int i = 0; i < 7; ++i)
	{
		registerProvider(kOptionsExterns[i], [this, i](const std::string &, std::string &value, bool setting) { return provide(i, value, setting); });
	}
	// RW 0x921586 .. 0x9215A6: APT:VersionNum is the version text
	const std::u16string version = versionText(m_env.gameText);
	std::string utf8;
	for (char16_t c : version)
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
	windows.setAptText("APT:VersionNum", utf8);
	windows.note("options-soft-particles", "[S-1484] Options: the OpenBFME Soft particles box (not a retail control) at stage (" + std::to_string(kSoftBoxX) + ", " +
		std::to_string(kSoftBoxY) + "), saved as Options.ini's SoftParticles");
	windows.note("options-unported", "[S-1913] Options: the advanced page, resolution / brightness apply, the LOD manager's master option, RefreshNat and live slider tracking are not ported");
	ensureSoftBox();
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
	m_allowResolution = allowResolutionChange;
	m_allowAdvanced = allowAdvanced;
	m_advancedOnly = advancedOnly;
}

bool AptOptionsScreen::provide(int index, std::string &value, bool setting)
{
	// RW 0x91F7F5 (a read answers; only MasterOption0Current takes a write)
	if (setting)
	{
		if (index == 1)
		{
			m_masterOption = equalsNoCase(value, "Custom") ? 5 : std::atoi(value.c_str());
			windows().note("options-unported", "MasterOption0Current = " + value + ": the LOD template is not applied [S-1913]");
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
		case 6: value = m_advancedOnly ? "1" : "0"; return true;
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
		static const std::pair<const char *, int> kDetail[6] = { { "GUI:UltraHigh", 4 }, { "GUI:High", 3 }, { "GUI:Medium", 2 }, { "GUI:Low", 1 },
			{ "GUI:VeryLow", 0 }, { "GUI:Custom", 5 } };
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
		for (int i = 0; i < 6; ++i)
		{
			if ((int)(std::uintptr_t)GadgetComboBoxGetItemData(w, i) == m_masterOption)
			{
				GadgetComboBoxSetSelectedPos(w, i, false);
				break;
			}
		}
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
			GadgetSliderSetPosition(w, initialVolume(t));
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
		// RW 0x91EBD1: checked from AllHealthBars (RW 0x6E6179: "yes", missing false) while the detail allows the box (RW 0x91EB77, the LOD
		// manager: S-1913, taken as allowed)
		GadgetCheckBoxSetChecked(w, yes("AllHealthBars", false));
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
		return;
	}
	if (name == "HighAudioQuality")
	{
		// AudioLOD's index (RW 0x6025F1, case-insensitive: Low 0, High 1; missing -1): checked when 1
		const bool high = prefs && prefs->has("AudioLOD") && equalsNoCase(prefs->get("AudioLOD"), kAudioLOD[1]);
		GadgetCheckBoxSetChecked(w, high);
		return;
	}
	windows().note("unported-gadget-init", "AptOptions::InitGadgets(" + fullName + ")");
}

void AptOptionsScreen::reset()
{
	// RW 0x91F4D1 (the basic page)
	if (m_state == 1)
	{
		if (GameWindow *b = gadget("Brightness"))
		{
			int mn = 0, mx = 0;
			GadgetSliderGetMinMax(b, &mn, &mx);
			GadgetSliderSetPosition(b, (mx - mn) / 2 + mn);
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
				GadgetSliderSetPosition(w, (int)(m_env.defaultVolumes[t] * 100.0f));
			}
		}
		if (GameWindow *hb = gadget("HealthBars"))
		{
			GadgetCheckBoxSetChecked(hb, m_env.options && m_env.options->getYesNo("AllHealthBars", false)); // RW 0x91EBD1
		}
	}
	// the OpenBFME box goes back to its default (soft)
	m_pendingSoft = true;
	if (m_softBox)
	{
		GadgetCheckBoxSetChecked(m_softBox, true);
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
	for (int t = 0; t < 5; ++t)
	{
		char buf[64];
		std::snprintf(buf, sizeof buf, "%f", (double)initialVolume(t));
		m_services.applyOption(kVolumeKeys[t], buf);
	}
	windows().requestShellPop();
}

AptOptionsScreen::~AptOptionsScreen()
{
	if (m_softBox && windows().gadgetLayer())
	{
		windows().gadgetLayer()->destroyEngineGadget(m_softBox);
	}
	m_softBox = nullptr;
}

void AptOptionsScreen::ensureSoftBox()
{
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (m_softBox || !layer || !ownerWindow())
	{
		return;
	}
	std::string error;
	m_softBox = layer->createEngineGadget("window/apt/checkbox.wnd", kSoftBoxX, kSoftBoxY, kSoftBoxW, kSoftBoxH, ownerWindow(), &error);
	if (!m_softBox)
	{
		windows().note("options-soft-particles-failed", "[S-1484] the Soft particles box: " + error);
		return;
	}
	// the port's own label (not a retail string: no lotr.str label exists for it)
	GadgetCheckBoxSetText(m_softBox, u"Soft particles");
	GadgetCheckBoxSetChecked(m_softBox, m_pendingSoft);
}

void AptOptionsScreen::runInit()
{
	AptScreen::runInit();
	ensureSoftBox();
	if (m_softBox)
	{
		m_softBox->winHide(false);
	}
}

void AptOptionsScreen::runShutdown(bool *immediate)
{
	if (m_softBox)
	{
		m_softBox->winHide(true);
	}
	AptScreen::runShutdown(immediate);
}

WindowMsgHandledType AptOptionsScreen::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	if (msg == GBM_SELECTED && m_softBox && reinterpret_cast<GameWindow *>(data1) == m_softBox)
	{
		m_pendingSoft = GadgetCheckBoxIsChecked(m_softBox);
		return MSG_HANDLED;
	}
	return AptScreen::gadgetMessage(from, msg, data1, data2);
}

void AptOptionsScreen::save()
{
	// RW 0x91FC9C: the basic page's entries (+ 0x27C == 1), written, then the screen closes (RW 0x91EA39)
	OptionPreferences *prefs = m_env.options;
	if (m_state == 1 && prefs)
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
		if (gadget("HealthBars"))
		{
			prefs->setYesNo("AllHealthBars", checked("HealthBars")); // while the detail allows the box (RW 0x91EB77: S-1913)
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
	else if (m_state != 1)
	{
		windows().note("options-save", "Save before OnInitialized: the basic page is not written (RW 0x91FCB2)");
	}
	if (prefs)
	{
		prefs->setSoftParticles(m_pendingSoft);
		std::string error;
		if (m_env.optionsFile.empty() || !prefs->save(m_env.optionsFile, &error))
		{
			windows().note("options-save-failed", m_env.optionsFile.empty() ? std::string("Options.ini: no file name") : error);
		}
		m_services.applyOption(OptionPreferences::kSoftParticles, prefs->get(OptionPreferences::kSoftParticles));
	}
	windows().requestShellPop();
}
