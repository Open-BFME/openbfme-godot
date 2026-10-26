// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptQuitMenu.h (lane END-2).

#include "GameClient/GUI/AptScreens/AptQuitMenu.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"

namespace
{
std::string text(const ShellEnvironment &env, const std::string &label)
{
	return loadScreenU16ToUtf8(fetchOrMissing(env.gameText, label));
}
} // namespace

AptQuitMenu::AptQuitMenu(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "QuitMenu.apt", "AptQuitMenu"), m_env(environment)
{
	registerCommand("AptQuitMenu::OnInitialized", [this](const std::string &) { onInitialized(); });
	registerCommand("AptQuitMenu::RestartMission", [this](const std::string &argument) {
		// RW 0x92175F sets + 0x27E; the update RW 0x9226AA picks the branch
		const QuitMenuContext *c = m_env.quitMenu;
		if (c && !c->isMultiplayer() && c->gameKind == 3)
		{
			m_keepBackground = c->gameMode == 2; // lane SAVE-UI1: RW 0x9226D6 .. 0x9226DF (+ 0x27D in a skirmish: the background stays through the restart)
			post((int)ShellAction::QuitMenuRestart, "QuitMenuRestart", argument); // RW 0x9220DE
		}
		else
		{
			post((int)ShellAction::QuitMenuForfeit, "QuitMenuForfeit", argument); // RW 0x921841
		}
	});
	registerCommand("AptQuitMenu::ExitMission", [this](const std::string &argument) {
		m_exiting = true; // lane SAVE-UI1: RW 0x92176B (+ 0x27C)
		post((int)ShellAction::QuitMenuExit, "QuitMenuExit", argument);
	});
	registerCommand("AptQuitMenu::ReturnToGame", [this](const std::string &argument) { post((int)ShellAction::QuitMenuReturn, "QuitMenuReturn", argument); });
	registerCommand("AptQuitMenu::OptionsScreen", [this](const std::string &argument) { post((int)ShellAction::QuitMenuOptions, "QuitMenuOptions", argument); });
	// lane SAVE-2: RW 0x92179F / 0x9217F0 (BFME2 decomp AptQuitMenuCallbacks.cpp SaveMenu / LoadMenu, tier A): AptSaveLoad::OpenScreen(3 save / 2 load, the
	// game's type, 1 = over a game); the type is 8 without TheGameLogic + 0x114 (the Living World), 16 by the decomp's Rva00210C66 field test, 4 in a multiplayer game, else
	// (TheGameLogic + 0x110 == 2) + 1: 2 Skirmish, 1 Campaign. The host opens the screen (the Living World and multiplayer types are not offered here: a
	// multiplayer game disables both buttons, RW 0x921D5B)
	registerCommand("AptQuitMenu::SaveMenu", [this](const std::string &) { post((int)ShellAction::QuitMenuSave, "QuitMenuSave", saveLoadType()); });
	registerCommand("AptQuitMenu::LoadMenu", [this](const std::string &) { post((int)ShellAction::QuitMenuLoad, "QuitMenuLoad", saveLoadType()); });
	registerTooltip("QuitMenu/Restart/TheButton", [this](const std::string &) { return restartTooltip(); });
	// RW 0x9216EC over the table RW 0xC7DB28
	registerProvider("HasFocus", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "1";
		}
		return true;
	});
	registerProvider("AptQuitMenu::RestartPopupType", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = restartPopupType();
		}
		return true;
	});
	// RW 0x922460 .. 0x9224A4: a multiplayer game shows GUI:Menu where the movie shows APT:Pause
	if (m_env.quitMenu && m_env.quitMenu->inGame && m_env.quitMenu->isMultiplayer())
	{
		windows.setAptText("APT:Pause", text(m_env, "GUI:Menu"));
	}
	// lane SAVE-UI1: the constructor ends with the in-game background (RW 0x922622 .. 0x92262A: RW 0x6230B6(2), ShowInGameBackground); a screen opened
	// over the menu (SaveLoad.apt, Options.apt) finds a background up and shows none of its own (SaveLoad's RW 0x818BC2)
	windows.setBackground(2);
}

AptQuitMenu::~AptQuitMenu()
{
	// lane CHEAT-1: nothing of the Cheat Mode panel may run after the menu is gone
	if (m_cheatLabel.size() || m_cheatPage)
	{
		windows().setMouseFilter(nullptr);
	}
	if (AptGadgetLayer *layer = windows().gadgetLayer())
	{
		if (m_cheatPage)
		{
			m_cheatPage->hide();
		}
		layer->clearDecorator(this);
	}
	// lane SAVE-UI1: RW 0x9219B9 / 0x921A70 .. 0x921A96: after Exit (+ 0x27C) the front-end background (RW 0x6230B6(1)); else, unless a skirmish restart
	// keeps it (+ 0x27D), the background is hidden with its animation (RW 0x622C88(0)) and the game is seen again
	if (m_exiting)
	{
		windows().setBackground(1);
	}
	else if (!m_keepBackground)
	{
		windows().hideBackground(false);
	}
	windows().setAptText("APT:Pause", text(m_env, "APT:Pause")); // RW 0x921A9B
}

std::string AptQuitMenu::saveLoadType() const
{
	const QuitMenuContext *c = m_env.quitMenu;
	if (c && c->isMultiplayer())
	{
		return "4";
	}
	return (c && c->gameMode == 2) ? "2" : "1";
}

std::string AptQuitMenu::restartPopupType() const
{
	const QuitMenuContext *c = m_env.quitMenu;
	return (!c || !c->inGame || c->gameKind == 3) ? "Restart" : "Forfeit"; // "Surrender" belongs to the Living World (not ported)
}

std::string AptQuitMenu::restartTooltip() const
{
	const QuitMenuContext *c = m_env.quitMenu;
	return (!c || !c->inGame || c->gameKind == 3) ? "TOOLTIP:QuitMenu/Restart/TheButton" : "TOOLTIP:QuitMenu/Forfeit/WOTRForfeit";
}

void AptQuitMenu::post(int action, const std::string &name, const std::string &argument)
{
	m_requests.push_back(name);
	if (m_env.services)
	{
		m_env.services->request(ShellRequest{ (ShellAction)action, argument });
	}
	else
	{
		windows().note("command-unwired", name + ": the host gave no shell services [S-1064]");
	}
}

// RW 0x921D5B
void AptQuitMenu::onInitialized()
{
	m_disabled.clear();
	const QuitMenuContext *c = m_env.quitMenu;
	auto disable = [this](const char *button) {
		m_disabled.push_back(button);
		std::string error;
		if (!windows().invokeAS(level(), "disableButton", { button }, nullptr, &error))
		{
			windows().note("quitmenu-disable", std::string("disableButton(") + button + "): " + error);
		}
	};
	if (c && c->inGame && (c->isMultiplayer() || c->replay))
	{
		if (c->gameKind == 3 || c->localPlayerDefeated)
		{
			disable("Restart");
		}
		disable("Save"); // RW 0x921E3C (DAT 0xC4FF8C)
		disable("Load"); // RW 0x921F83 (DAT 0xC4FF84)
	}
	const bool restart = !c || !c->inGame || c->gameKind == 3;
	windows().setAptText("APT:RestartOrSurrender", text(m_env, restart ? "APT:Restart" : "APT:Forfeit"));
}

// ---- lane CHEAT-1: the Cheat Mode panel (see AptQuitMenu.h) ---------------------------------------------------------------------------------------------

void AptQuitMenu::runInit()
{
	AptScreen::runInit();
	if (!m_cheatLabel.empty() || cheatPageOpen())
	{
		takeHooks(); // a screen opened over the menu (Options) took the mouse filter and the decorator and cleared them on its way out
	}
}

void AptQuitMenu::takeHooks()
{
	windows().setMouseFilter([this](int kind, float x, float y) { return cheatMouse(kind, x, y); });
	if (AptGadgetLayer *layer = windows().gadgetLayer())
	{
		layer->setDecorator([this](GadgetDrawList &out) { decorate(out); }, this);
	}
}

void AptQuitMenu::setCheatButton(const UnicodeString &label)
{
	m_cheatLabel = label;
	m_cheatHot = m_cheatPressed = false;
	takeHooks();
}

bool AptQuitMenu::openCheatPage(const OpenBFMEPageSpec &spec, std::string *error)
{
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (!layer || !ownerWindow())
	{
		if (error)
		{
			*error = "no gadget layer";
		}
		return false;
	}
	if (!m_cheatPage)
	{
		m_cheatPage = std::make_unique<OpenBFMEOptionsPage>(*layer, ownerWindow());
		m_cheatPage->setSink([this](const OpenBFMEPageEvent &e) {
			if (m_cheatEvents.size() < 256)
			{
				m_cheatEvents.push_back(e);
			}
		});
	}
	OpenBFMEPageSpec s = spec;
	s.backdrop = true;
	if (!m_cheatPage->show(s, error))
	{
		return false;
	}
	takeHooks();
	return true;
}

void AptQuitMenu::closeCheatPage()
{
	if (m_cheatPage)
	{
		m_cheatPage->hide();
	}
}

std::vector<OpenBFMEPageEvent> AptQuitMenu::takeCheatEvents()
{
	std::vector<OpenBFMEPageEvent> out;
	out.swap(m_cheatEvents);
	return out;
}

WindowMsgHandledType AptQuitMenu::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	if (cheatPageOpen() && m_cheatPage->message(msg, data1, data2))
	{
		return MSG_HANDLED;
	}
	return AptScreen::gadgetMessage(from, msg, data1, data2);
}

bool AptQuitMenu::cheatMouse(int kind, float x, float y)
{
	if (isHidden() || shell().inputTop() != this) // the in-game menu is the shell's overlay (lane OPTS-6): it is on top while nothing covers it
	{
		return false;
	}
	if (cheatPageOpen())
	{
		if (m_cheatPage->mouse(kind, x, y))
		{
			return true;
		}
		// the page's band is the page's: a press there never reaches the menu's buttons under it; its own gadgets still take it from the gadget layer
		if (kind != 0 && x >= OpenBFMEOptionsPage::kTabsLeft - 8 && x <= OpenBFMEOptionsPage::kPanelRight + 8 && y >= OpenBFMEOptionsPage::kTop - 8 &&
			y <= OpenBFMEOptionsPage::kBottom + 8)
		{
			if (AptGadgetLayer *layer = windows().gadgetLayer())
			{
				layer->mouseButton(kind == 1);
			}
			return true;
		}
		return false;
	}
	if (m_cheatLabel.empty())
	{
		m_cheatHot = m_cheatPressed = false;
		return false;
	}
	const bool inside = x >= kCheatButtonLeft && x <= kCheatButtonRight && y >= kCheatButtonTop && y <= kCheatButtonBottom;
	if (kind == 0)
	{
		m_cheatHot = inside;
		return false;
	}
	if (kind == 1 && inside)
	{
		m_cheatPressed = true;
		return true;
	}
	if (kind == 2 && m_cheatPressed)
	{
		m_cheatPressed = false;
		if (inside)
		{
			OpenBFMEPageEvent e;
			e.kind = OpenBFMEPageEvent::Kind::Entry;
			e.key = "cheat_button";
			m_cheatEvents.push_back(e);
		}
		return true;
	}
	return false;
}

void AptQuitMenu::decorate(GadgetDrawList &out)
{
	if (isHidden() || shell().inputTop() != this) // the in-game menu is the shell's overlay (lane OPTS-6): it is on top while nothing covers it
	{
		return;
	}
	if (cheatPageOpen())
	{
		m_cheatPage->decorate(out);
		return;
	}
	if (m_cheatLabel.empty())
	{
		return;
	}
	// the FRIENDS button's look (AptOnlineShell::decorateAll): a dark box, the page's frame, the bottom bar's lettering, brighter when pointed
	GadgetDrawCommand c;
	c.kind = GadgetDrawCommand::Kind::FillRect;
	c.window = "OpenBFMECheatMode";
	c.x0 = kCheatButtonLeft;
	c.y0 = kCheatButtonTop;
	c.x1 = kCheatButtonRight;
	c.y1 = kCheatButtonBottom;
	c.color = m_cheatPressed ? 0xF0182028u : 0xE0050607u;
	out.commands.push_back(c);
	OpenBFMEOptionsPage::frame(out, kCheatButtonLeft, kCheatButtonTop, kCheatButtonRight, kCheatButtonBottom, UnicodeString());
	OpenBFMEOptionsPage::text(out, m_cheatLabel, kCheatButtonLeft, kCheatButtonTop + 9, 14,
		m_cheatHot ? OpenBFMEOptionsPage::kTabHotColor : OpenBFMEOptionsPage::kTabColor, kCheatButtonRight - kCheatButtonLeft, true);
}
