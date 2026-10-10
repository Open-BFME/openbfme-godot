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

#include <memory>
#include <string>
#include <vector>

class AptMessageBox;
class CreditsManager;
struct ShellEnvironment;

class AptMainMenu : public AptScreen
{
public:
	// `environment` (lane UI-4): the archives and language.ini's fonts the credits roll reads; null: Credits reports that it cannot roll
	AptMainMenu(WindowManager &windows, Shell &shell, ShellServices &services, const ShellEnvironment *environment = nullptr);
	~AptMainMenu() override;

	// The RotWK names in binary order, for the registry test (commands and the render component).
	static const std::vector<std::string> &retailNames();

	// The provider state (what the retail providers answer from preferences and campaign progress; not wired: reported).
	bool optionsAdvanced() const { return m_optionsAdvanced; }

	// lane CAH-2 (stop S-1914): the host has no screen for the request `action` (a ShellAction name, e.g. "CreateAHero"). Not retail: retail always
	// has the screen. The movie already ran DisableAllButtons, so the player would be left with a dead menu; instead the gap reaches the player as an
	// Ok message box ("<screen> is not available in this build yet", AptMessageBox, RW 0x953861) and the report (note "unported-screen"), and the box's
	// Ok gives the menu what a sub-screen's return gives it: OnFocus("1") (WindowManager::refreshFocus RW 0x46E170 after the pop), whose
	// ShowMainMenu reveals the buttons again. Returns the stop line ("" for a request that opens no screen: nothing shown).
	std::string screenUnavailable(const std::string &action);
	// the screen name the box names for `action` ("" for a request that opens no screen)
	static std::string unavailableScreenName(const std::string &action);
	AptMessageBox *unavailableBox() { return m_box.get(); }

	// lane UI-4: the credits page (TARGET FACTS, RotWK game.dat, caveat S-001; BFME2 decomp AptMainMenuCallbacks.cpp, tier A for both):
	// - AptMainMenu::Credits (RW 0x91B5E9): the old roll deleted, a new CreditsManager (GameClient/Credits.h) loaded (vslot 2) and started (vslot 1)
	//   into the global RW 0xDEBF50; the transition group MainMenuToCreditsScreen; the shell's music nudged (RW 0x35BD3F's twin) and the misc audio's
	//   CreditsMusic played; the menu state + 0x288 = 4; Shell + 0x5D set; the engine's frame rate limit 100;
	// - the menu's update (RW 0x91C2FC) in state 4: the roll's update (vslot 10); once it is finished the movie is asked for "HideCredits" (RW 0xC7CDBC
	//   through RW 0x62279C; MainMenu.apt defines no such function, so nothing happens and the page stays until Exit);
	// - AptMainMenu::CreditsExit (RW 0x91B6FD, the tail of the same tier-A run as Credits; BFME2 0x915231): the roll reset (vslot 9) and deleted, the shell's music restored, the
	//   transition reversed, state 0, Shell + 0x5D cleared, the frame rate limit back to GlobalData's;
	// - the render callback AptMainMenu::RenderCredits (RW 0x91B1B8): with a roll, its draw (RW 0x9C6765) with the clip's position and size.
	// DEVICE / INFERENCE (stop S-2520): the roll steps once per 10 ms of the shell's clock (the 100 frames a second the engine is limited to while
	// it rolls), at most 25 steps per update; the window transition MainMenuToCreditsScreen and Shell + 0x5D are not ported; the
	// music is the host's (the ShellRequests Credits / CreditsExit).
	const CreditsManager *credits() const { return m_credits.get(); }
	int menuState() const { return m_state; }
	// one roll step per call, as the menu's update in state 4 runs it (tests)
	void stepCredits();

private:
	void registerAll();
	void showSkirmish();
	void request(ShellAction action, const std::string &argument);

	void update();
	void startCredits();
	void exitCredits();

	ShellServices &m_services;
	bool m_optionsAdvanced = false;
	std::unique_ptr<AptMessageBox> m_box; // lane CAH-2: made on demand, released after its Ok (GuiFX.apt can be loaded once: the lobbies own their own)
	bool m_boxClosed = false;
	const ShellEnvironment *m_env = nullptr;   // lane UI-4
	std::unique_ptr<CreditsManager> m_credits; // RW 0xDEBF50
	int m_state = 0;                           // + 0x288 (4: the credits roll)
	int m_creditsClockMs = 0;                  // the roll's 10 ms steps not yet run
	bool m_hideCreditsAsked = false;
};
