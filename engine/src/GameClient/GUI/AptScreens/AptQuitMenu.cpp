// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptQuitMenu.h (lane END-2).

#include "GameClient/GUI/AptScreens/AptQuitMenu.h"

#include "GameClient/GUI/GameTextSource.h"
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
			post((int)ShellAction::QuitMenuRestart, "QuitMenuRestart", argument); // RW 0x9220DE
		}
		else
		{
			post((int)ShellAction::QuitMenuForfeit, "QuitMenuForfeit", argument); // RW 0x921841
		}
	});
	registerCommand("AptQuitMenu::ExitMission", [this](const std::string &argument) { post((int)ShellAction::QuitMenuExit, "QuitMenuExit", argument); });
	registerCommand("AptQuitMenu::ReturnToGame", [this](const std::string &argument) { post((int)ShellAction::QuitMenuReturn, "QuitMenuReturn", argument); });
	registerCommand("AptQuitMenu::OptionsScreen", [this](const std::string &argument) { post((int)ShellAction::QuitMenuOptions, "QuitMenuOptions", argument); });
	registerCommand("AptQuitMenu::SaveMenu", unportedCommand("AptQuitMenu::SaveMenu"));
	registerCommand("AptQuitMenu::LoadMenu", unportedCommand("AptQuitMenu::LoadMenu"));
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
}

AptQuitMenu::~AptQuitMenu()
{
	windows().setAptText("APT:Pause", text(m_env, "APT:Pause")); // RW 0x921A9B
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
