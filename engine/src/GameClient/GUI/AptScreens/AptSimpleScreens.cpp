// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptSimpleScreens.h.

#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/OptionPreferences.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"

#include "GameClient/GUI/GameTextSource.h"

#include <cstdio>

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

AptOptionsScreen::AptOptionsScreen(WindowManager &windows, Shell &shell, ShellEnvironment &environment, ShellServices &services)
	: AptScreen(windows, shell, "Options.apt", "AptOptions"), m_env(environment), m_services(services)
{
	m_pendingSoft = m_env.options ? m_env.options->softParticles() : true;
	if (!m_env.options)
	{
		windows.note("options-unwired", "Options: no OptionPreferences: Save writes nothing [S-1484]");
	}
	registerCommand("AptOptions::OnInitialized", unportedCommand("AptOptions::OnInitialized"));
	// RW 0x91EC52: the retail controls' restore is not ported (S-175); the screen closes (RW 0x91EA39)
	registerCommand("AptOptions::Cancel", [this](const std::string &) {
		this->windows().note("unported-command", "AptOptions::Cancel: the retail controls' restore [S-175]");
		this->windows().requestShellPop();
	});
	registerCommand("AptOptions::Save", [this](const std::string &) { save(); });
	// RW 0x91F4D1 resets the retail controls (not ported, S-175); the OpenBFME box goes back to its default (soft)
	registerCommand("AptOptions::Reset", [this](const std::string &) {
		this->windows().note("unported-command", "AptOptions::Reset: the retail controls [S-175]");
		m_pendingSoft = true;
		if (m_softBox)
		{
			GadgetCheckBoxSetChecked(m_softBox, true);
		}
	});
	registerCommand("AptOptions::RefreshNat", unportedCommand("AptOptions::RefreshNat"));
	registerCommand("AptOptions::EnterAdvancedSettings", unportedCommand("AptOptions::EnterAdvancedSettings"));
	registerScreenRef("AptOptions::InitGadgets", [this](const std::string &name, GameWindow *) {
		this->windows().note("unported-gadget-init", "AptOptions::InitGadgets(" + name + ") [S-175]");
	});
	windows.note("options-soft-particles", "[S-1484] Options: the OpenBFME Soft particles box (not a retail control) at stage (" + std::to_string(kSoftBoxX) + ", " +
		std::to_string(kSoftBoxY) + "), saved as Options.ini's SoftParticles; the other retail options are not saved (S-175)");
	ensureSoftBox();
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
	// RW 0x91FC9C: the preferences are set and written, then the screen closes (RW 0x91EA39); the retail controls' entries are not set (S-175)
	windows().note("unported-command", "AptOptions::Save: the retail controls' entries [S-175]");
	if (m_env.options)
	{
		m_env.options->setSoftParticles(m_pendingSoft);
		std::string error;
		if (m_env.optionsFile.empty() || !m_env.options->save(m_env.optionsFile, &error))
		{
			windows().note("options-save-failed", m_env.optionsFile.empty() ? std::string("Options.ini: no file name") : error);
		}
		m_services.applyOption(OptionPreferences::kSoftParticles, m_env.options->get(OptionPreferences::kSoftParticles));
	}
	windows().requestShellPop();
}
