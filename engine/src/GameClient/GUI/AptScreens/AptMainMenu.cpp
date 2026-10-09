// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptMainMenu.h.

#include "GameClient/GUI/AptScreens/AptMainMenu.h"

#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellServices.h"

namespace
{
struct CommandRow
{
	const char *name;
	ShellAction action;
};

// Commands that become a ShellRequest (their engine behaviour belongs to other lanes).
const CommandRow kRequestCommands[] = {
	{ "AptMainMenu::LoadGame", ShellAction::LoadGame },
	{ "AptMainMenu::LoadReplay", ShellAction::LoadReplay },
	{ "AptMainMenu::LAN", ShellAction::Lan },
	{ "AptMainMenu::OnlineButtonPressed", ShellAction::Online },
	{ "AptMainMenu::BattleSchool", ShellAction::BattleSchool },
	{ "AptMainMenu::LevelSelect", ShellAction::LevelSelect },
	{ "AptMainMenu::LoadCampaign", ShellAction::LoadCampaign },
	{ "AptMainMenu::ContinueCampaign", ShellAction::ContinueCampaign },
	{ "AptMainMenu::Expansion1Campaign", ShellAction::Expansion1Campaign },
	{ "AptMainMenu::BonusCampaign", ShellAction::BonusCampaign },
	{ "AptMainMenu::WarOfTheRing", ShellAction::WarOfTheRing },
	{ "AptMainMenu::CreateAHero", ShellAction::CreateAHero },
	{ "AptMainMenu::OnTutorial", ShellAction::Tutorial },
	{ "AptMainMenu::Credits", ShellAction::Credits },
	{ "AptMainMenu::CreditsExit", ShellAction::CreditsExit },
	{ "AptMainMenu::StopGameMovie", ShellAction::StopGameMovie },
	{ "AptMainMenu::ResetResolution", ShellAction::ResetResolution },
};
} // namespace

const std::vector<std::string> &AptMainMenu::retailNames()
{
	static const std::vector<std::string> names = {
		"AptMainMenu::RenderCredits", "AptMainMenu::OnTutorial", "AptMainMenu::ResetResolution", "AptMainMenu::StopGameMovie",
		"AptMainMenu::BattleSchool", "AptMainMenu::OnlineButtonPressed", "AptMainMenu::LAN", "AptMainMenu::LevelSelect",
		"AptMainMenu::LoadReplay", "AptMainMenu::LoadGame", "AptMainMenu::ExitGame", "AptMainMenu::CreditsExit",
		"AptMainMenu::Credits", "AptMainMenu::CreateAHero", "AptMainMenu::Options", "AptMainMenu::Skirmish",
		"AptMainMenu::LoadCampaign", "AptMainMenu::ContinueCampaign", "AptMainMenu::WarOfTheRing", "AptMainMenu::BonusCampaign",
		"AptMainMenu::Expansion1Campaign", "AptMainMenu::OnInitialized"
	};
	return names;
}

AptMainMenu::AptMainMenu(WindowManager &windows, Shell &shell, ShellServices &services)
	: AptScreen(windows, shell, "MainMenu.apt", "AptMainMenu"), m_services(services)
{
	registerAll();
}

void AptMainMenu::request(ShellAction action, const std::string &argument)
{
	ShellRequest r;
	r.action = action;
	r.argument = argument;
	m_services.request(r);
}

void AptMainMenu::showSkirmish()
{
	// ShowSkirmish.cpp (0x00579440): push Skirmish.apt unless its singleton already exists
	if (!shell().findScreenByFilename("Skirmish.apt"))
	{
		shell().push("Skirmish.apt", false);
	}
}

void AptMainMenu::registerAll()
{
	registerCommand("AptMainMenu::OnInitialized", [this](const std::string &) {
		// the retail body sets preference counters (TimesInGame, FlashTutorial) and the logo mapped image; not ported [S-175]
		windows().note("unported-command", "AptMainMenu::OnInitialized: preference bookkeeping");
	});
	registerCommand("AptMainMenu::Skirmish", [this](const std::string &) { showSkirmish(); });
	registerCommand("AptMainMenu::Options", [this](const std::string &argument) {
		// MainMenu calls GameCode('Options', true|false): advanced or basic (spec 1.10); the Options screen's own use of the
		// flag is not ported [S-175]
		m_optionsAdvanced = argument == "true" || argument == "1";
		if (!shell().findScreenByFilename("Options.apt"))
		{
			shell().push("Options.apt", false);
		}
	});
	registerCommand("AptMainMenu::ExitGame", [this](const std::string &argument) {
		// AptMainMenuExitGame.cpp: script hook, sound, TheShell->pop(), TheGameEngine->setQuitting(true).  The pop runs on the next
		// WindowManager::update (the shell cannot be torn down from inside a movie script)
		request(ShellAction::ExitGame, argument);
		windows().requestShellPop();
	});
	for (const CommandRow &row : kRequestCommands)
	{
		const ShellAction action = row.action;
		const std::string name = row.name;
		registerCommand(name, [this, action, name](const std::string &argument) {
			request(action, argument);
			windows().note("unported-command", name + (argument.empty() ? std::string() : "(" + argument + ")") + ": the request is the whole behaviour so far [S-175]");
		});
	}
	// providers: the values come from preferences and campaign progress, which are not wired [S-175]
	registerProvider("MainMenuUnlockBonusCampaign", unwiredProvider("MainMenuUnlockBonusCampaign"));
	registerProvider("MainMenuLevel", unwiredProvider("MainMenuLevel"));
	registerProvider("MainMenuContinueCampaign", unwiredProvider("MainMenuContinueCampaign"));
	registerProvider("BlinkBattleSchoolOff", unwiredProvider("BlinkBattleSchoolOff"));
	// the credits render callback is a component (registerAptCallback): the device draws it, no native object is kept
	registerComponent("AptMainMenu::RenderCredits", [this](AptComponentRequest &) -> std::shared_ptr<GameWindow> {
		windows().note("unported-component", "AptMainMenu::RenderCredits [S-175]");
		return nullptr;
	});
}
