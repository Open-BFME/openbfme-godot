// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptMainMenu.h.

#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/WindowManager.h"

#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/Credits.h"
#include "Common/INIException.h"

#include <algorithm>

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

AptMainMenu::AptMainMenu(WindowManager &windows, Shell &shell, ShellServices &services, const ShellEnvironment *environment)
	: AptScreen(windows, shell, "MainMenu.apt", "AptMainMenu"), m_services(services), m_env(environment)
{
	// lane FB7-1: the ctor (RW 0x91C9A9, first instance) binds the mapped image LogoWithShadow to the movie's RenderImage clip `Image` through
	// RW 0x6236F6 (WindowManager, image record by name; RW 0x91CA5C .. 0x91CA98): the game's logo over the front-end background
	windows.setAptImage("Image", "LogoWithShadow");
	registerAll();
	windows.addUpdateListener(this, [this]() { update(); });
}

AptMainMenu::~AptMainMenu()
{
	windows().clearRenderCallback("AptMainMenu::RenderCredits");
}

std::string AptMainMenu::unavailableScreenName(const std::string &action)
{
	// the main menu's requests that open a screen of their own (the names as the menu's buttons call them). Not Credits: MainMenu.apt's CreditsButton
	// plays its own _CreditsMovie clip with an exit button (ExitCreditsButton -> ShowMainMenu), the command starts the roll (lane UI-4)
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
	if (m_state == 4 && m_credits)
	{
		// RW 0x91C631: the roll's update each engine frame; the engine is limited to 100 frames a second while it rolls (RW 0x91B6DA), so the port
		// runs one step per 10 ms of the shell's clock (INFERENCE, S-2520)
		m_creditsClockMs += std::max(0, windows().lastElapsedMs());
		int steps = 0;
		while (m_creditsClockMs >= 10 && steps < 25)
		{
			m_creditsClockMs -= 10;
			stepCredits();
			++steps;
		}
		if (steps == 25)
		{
			m_creditsClockMs = 0; // a stall is not replayed
		}
	}
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

void AptMainMenu::stepCredits()
{
	if (!m_credits)
	{
		return;
	}
	m_credits->update();
	if (m_credits->isFinished() && !m_hideCreditsAsked)
	{
		// RW 0x91C646 .. 0x91C66C: the movie's HideCredits (asked each frame in retail; the movie has no such member, so the port asks once and notes it)
		m_hideCreditsAsked = true;
		std::string error;
		if (!windows().invokeAS(level(), "HideCredits", {}, nullptr, &error))
		{
			windows().note("credits", "the roll finished; MainMenu has no HideCredits (" + error + "): the page stays until Exit, as in retail");
		}
	}
}

void AptMainMenu::startCredits()
{
	// RW 0x91B5E9
	m_credits.reset();
	m_hideCreditsAsked = false;
	m_creditsClockMs = 0;
	if (!m_env || !m_env->fileSystem || !m_env->language || !windows().gadgetLayer())
	{
		windows().note("credits", "[S-2520] the credits cannot roll: the shell environment has no archives, language.ini or font metrics");
	}
	else
	{
		auto credits = std::make_unique<CreditsManager>();
		int stageWidth = 1024;
		if (AptSpriteInst *root = windows().apt().level(level()))
		{
			if (root->timelineFile)
			{
				stageWidth = (int)root->timelineFile->width;
			}
		}
		try
		{
			credits->load(*m_env->fileSystem, m_env->gameText, *m_env->language, windows().gadgetLayer()->gadgets().fontMetrics(), stageWidth);
			credits->init();
			m_credits = std::move(credits);
		}
		catch (const INIException &e)
		{
			windows().note("credits", std::string("Data\\INI\\Credits.ini: ") + e.what());
		}
	}
	windows().note("unported-transition", "[S-2520] MainMenuToCreditsScreen: the credits page appears without its window transition");
	m_state = 4;
	request(ShellAction::Credits, std::string()); // the host: the shell music nudged, CreditsMusic, the frame rate
}

void AptMainMenu::exitCredits()
{
	// RW 0x91B6FD (BFME2 decomp AptMainMenu::CreditsExit, 0x915231)
	if (m_credits)
	{
		m_credits->reset();
		m_credits.reset();
	}
	m_state = 0;
	m_creditsClockMs = 0;
	request(ShellAction::CreditsExit, std::string()); // the host: the shell's music back
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
	registerCommand("AptMainMenu::Credits", [this](const std::string &) { startCredits(); });
	registerCommand("AptMainMenu::CreditsExit", [this](const std::string &) { exitCredits(); });
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
	// the credits render callback (registerAptCallback, RW 0x91D301 binds RW 0x91B1B8): the clip is a component with no window; the device asks the
	// render callback below for the text it draws in the clip's place
	registerComponent("AptMainMenu::RenderCredits", [](AptComponentRequest &) -> std::shared_ptr<GameWindow> { return nullptr; });
	windows().setRenderCallback("AptMainMenu::RenderCredits", [this](float x, float y, float w, float h, GadgetDrawList &out) {
		if (m_credits)
		{
			m_credits->draw(x, y, w, h, out); // RW 0x91B1B8: only with a roll ([RW 0xDEBF50] != 0)
		}
	});
}
