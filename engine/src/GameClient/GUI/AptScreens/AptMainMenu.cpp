// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptMainMenu.h.

#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/WindowManager.h"

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
	// lane FB7-1: the ctor (RW 0x91C9A9, first instance) binds the mapped image LogoWithShadow to the movie's RenderImage clip `Image` through
	// RW 0x6236F6 (WindowManager, image record by name; RW 0x91CA5C .. 0x91CA98): the game's logo over the front-end background
	windows.setAptImage("Image", "LogoWithShadow");
	registerAll();
	windows.addUpdateListener(this, [this]() { update(); });
}

AptMainMenu::~AptMainMenu() = default;

std::string AptMainMenu::unavailableScreenName(const std::string &action)
{
	// the main menu's requests that open a screen of their own (the names as the menu's buttons call them). Not Credits: MainMenu.apt's CreditsButton
	// plays its own _CreditsMovie clip with an exit button (ExitCreditsButton -> ShowMainMenu), the request only starts the scroll (RenderCredits, S-175)
	static const std::pair<const char *, const char *> kScreens[] = {
		{ "CreateAHero", "My Heroes (Create-a-Hero)" },
		{ "LoadGame", "Load Game" },
		{ "Online", "Online" },
		{ "BattleSchool", "Battle School" },
		{ "LevelSelect", "Level Select" },
		{ "LoadCampaign", "Load Campaign" },
		{ "ContinueCampaign", "Continue Campaign" },
		{ "ContinueCampaignNone", "Continue Campaign (no saved campaign progress)" },
		{ "Expansion1Campaign", "The Rise of the Witch-king campaign" },
		{ "BonusCampaign", "The bonus campaign" },
		{ "WarOfTheRing", "War of the Ring" },
		{ "Tutorial", "The tutorial" },
		{ "Lan", "Network" },
		{ "LoadReplay", "Replays" },
	};
	for (const auto &row : kScreens)
	{
		if (action == row.first)
		{
			return row.second;
		}
	}
	return std::string();
}

std::string AptMainMenu::screenUnavailable(const std::string &action)
{
	const std::string screen = unavailableScreenName(action);
	if (screen.empty())
	{
		return std::string(); // a request that opens no screen (StopGameMovie, ResetResolution ...): nothing is left disabled by it
	}
	const std::string line = "[S-1914] " + screen + " (AptMainMenu " + action + ") is not available in this build yet: the menu's request has no screen; the "
		"player is told and the main menu is usable again";
	windows().note("unported-screen", line);
	if (!m_box)
	{
		m_box = std::make_unique<AptMessageBox>(windows(), shell());
	}
	m_boxClosed = false;
	const std::string text = screen + " is not available in this build yet.";
	m_box->show(AptMessageBox::TYPE_OK, u"OpenBFME", std::u16string(text.begin(), text.end()), [this](int) { m_boxClosed = true; });
	if (m_box->level() < 0)
	{
		// GuiFX.apt did not load (noted by the box): the menu comes back at once so it is never left disabled
		m_boxClosed = true;
	}
	return line;
}

void AptMainMenu::update()
{
	if (!m_box)
	{
		return;
	}
	m_box->update();
	if (m_boxClosed)
	{
		// the box's Ok: the menu gets OnFocus("1") as after a sub-screen's pop (RW 0x46E170) and its ShowMainMenu reveals the buttons; the box is
		// released here, outside its own button command
		m_boxClosed = false;
		m_box.reset();
		std::string error;
		if (!windows().invokeAS(level(), "OnFocus", { "1" }, nullptr, &error))
		{
			windows().note("invoke-failed", "MainMenu OnFocus(1) after the unavailable-screen box: " + error);
		}
	}
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
		windows().setBackground(1); // lane FB7-1: RotWK's opener shows the front-end background (RW 0x928324 .. 0x928326)
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
		// MainMenu calls GameCode('Options', true|false): advanced or basic (spec 1.10). Lane FB7-1: the menu's state 9 opens it with
		// RW 0x91ED91(0, 1, 1, advanced) (RW 0x91C37E .. 0x91C38A): no network, the resolution and the advanced options allowed
		m_optionsAdvanced = argument == "true" || argument == "1";
		AptOptionsScreen::open(shell(), false, true, true, m_optionsAdvanced);
	});
	registerCommand("AptMainMenu::CreateAHero", [this](const std::string &argument) {
		// lane CAH-1, RW 0x91A018: CreateAHero.apt is pushed and the map Maps\CreateAHero\CreateAHero.map is started behind it (mode 7): the request
		// is the device's cue to start that map (the 3D view of the builder)
		if (!shell().findScreenByFilename("CreateAHero.apt"))
		{
			shell().push("CreateAHero.apt", false);
		}
		request(ShellAction::CreateAHero, argument);
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
