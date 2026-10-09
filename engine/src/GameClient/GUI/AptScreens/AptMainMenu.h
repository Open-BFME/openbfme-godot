// OpenBFME. GPL-3.0.
//
// AptMainMenu: the engine side of MainMenu.apt (CodePrefix "AptMainMenu"; spec menus-apt.md 1.2, 3.4 row "AptMainMenu").
//
// Target facts (RotWK game.dat string table, 0x0087CFD8..0x0087D274, S-001 caveat): the binary holds exactly 22 AptMainMenu::* names:
// RenderCredits, OnTutorial, ResetResolution, StopGameMovie, BattleSchool, OnlineButtonPressed, LAN, LevelSelect, LoadReplay,
// LoadGame, ExitGame, CreditsExit, Credits, CreateAHero, Options, Skirmish, LoadCampaign, ContinueCampaign, WarOfTheRing,
// BonusCampaign, Expansion1Campaign, OnInitialized.  The externs the movie reads are MainMenuUnlockBonusCampaign, MainMenuLevel,
// MainMenuContinueCampaign and BlinkBattleSchoolOff (spec 1.2).
// Donor facts (BFME1 AptMainMenuConstructor.cpp:329-506): the constructor registers the commands with bindShown, the providers
// with bindShownWithArg (argument 0 / 1 / 3 selecting the value) and RenderCredits as a render component; the registration happens
// only for the first instance (the g_rva012F49B4 singleton).  AptMainMenuExitGame.cpp: ExitGame signals the script engine, plays a
// sound, pops the shell and sets the engine quitting.  ShowSkirmish.cpp: Skirmish pushes Skirmish.apt unless its singleton exists.
// Inference / not ported (stop S-175): every other handler body (campaign start, LAN, online, credits ...) is not in the decompile
// (RotWK names with no BFME1 counterpart: "missing", spec 3.4); they report a ShellRequest and a note "unported-command".

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/ShellServices.h"

#include <string>
#include <vector>

class AptMainMenu : public AptScreen
{
public:
	AptMainMenu(WindowManager &windows, Shell &shell, ShellServices &services);

	// The RotWK names in binary order, for the registry test (commands and the render component).
	static const std::vector<std::string> &retailNames();

	// The provider state (what the retail providers answer from preferences and campaign progress; not wired: reported).
	bool optionsAdvanced() const { return m_optionsAdvanced; }

private:
	void registerAll();
	void showSkirmish();
	void request(ShellAction action, const std::string &argument);

	ShellServices &m_services;
	bool m_optionsAdvanced = false;
};
