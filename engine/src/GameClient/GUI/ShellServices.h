// OpenBFME. GPL-3.0.
//
// What the shell and its screens ask of the rest of the engine (menus-apt.md 3.3, step A4).  The WindowManager, Shell and the
// Apt screens are headless engine code; every effect outside them (audio, the application quit, starting a campaign, the
// new-game message of Skirmish) goes through this interface, so the tests record it and the devices implement it.
//
// A request is a fact the UI produced ("the player chose Exit"), not a behaviour: the engine behind it is another lane's.

#pragma once

#include <string>
#include <vector>

// The actions the main menu hands to the engine.  Retail's handlers (BFME1 AptMainMenu*.cpp, RotWK names only; spec 3.4 rows
// "AptMainMenu") start campaigns, push other shell screens or ask for the application to quit.
enum class ShellAction
{
	ExitGame,            // AptMainMenu::ExitGame
	LoadGame,            // AptMainMenu::LoadGame
	LoadReplay,          // AptMainMenu::LoadReplay
	Lan,                 // AptMainMenu::LAN
	Online,              // AptMainMenu::OnlineButtonPressed
	BattleSchool,        // AptMainMenu::BattleSchool
	LevelSelect,         // AptMainMenu::LevelSelect
	LoadCampaign,        // AptMainMenu::LoadCampaign
	ContinueCampaign,    // AptMainMenu::ContinueCampaign
	Expansion1Campaign,  // AptMainMenu::Expansion1Campaign
	BonusCampaign,       // AptMainMenu::BonusCampaign
	WarOfTheRing,        // AptMainMenu::WarOfTheRing
	CreateAHero,         // AptMainMenu::CreateAHero
	Tutorial,            // AptMainMenu::OnTutorial
	Credits,             // AptMainMenu::Credits
	CreditsExit,         // AptMainMenu::CreditsExit
	StopGameMovie,       // AptMainMenu::StopGameMovie
	ResetResolution,     // AptMainMenu::ResetResolution
	ScoreScreenContinue, // AptTimeLine::OnButtonContinue (lane END-1: RW 0x925699)
	ScoreScreenSaveReplay, // AptTimeLine::OnButtonSaveReplay (lane END-1: RW 0x924C83, not ported)
	LoadReplayFile,       // AptSaveLoad::Load on the replay page (lane MP-2: RW 0x816DBF -> RW 0x816981): the argument is the chosen file
	LanGameStart,         // AptLanLobby: every player of the LAN game has the start (lane MP-2): the host takes the LAN lobby's start and game socket
	// lane END-2: QuitMenu.apt (AptQuitMenu); the host acts on them (see AptQuitMenu.h)
	QuitMenuReturn,      // AptQuitMenu::ReturnToGame (RW 0x921794): close, code 0
	QuitMenuExit,        // AptQuitMenu::ExitMission (RW 0x921769): close, code 2, leave the game (RW 0x921904 -> 0x625E36)
	QuitMenuRestart,     // AptQuitMenu::RestartMission in a non-multiplayer game of kind 3 (RW 0x9226AA -> 0x9220DE): the same game again
	QuitMenuForfeit,     // AptQuitMenu::RestartMission otherwise (RW 0x9226AA -> 0x921841): close, MSG_SELF_DESTRUCT(false) unless the alliance won
	QuitMenuOptions,     // AptQuitMenu::OptionsScreen (RW 0x921783 -> 0x91ED91: the options screen over the game)
	ToggleQuitMenu,      // AptPalantir::OnBttnOptions (RW 0x6D40C1 -> 0x9220BD -> ToggleQuitMenu RW 0x921C9D)
	// lane PLAY-1: the Palantir's flag button AptPalantir::OnBttnObjectives (RW 0x6D40C9): in a skirmish or multiplayer game (RW 0x625456) RW 0x914EF0 pushes
	// PlayerTribute.apt over the game, otherwise RW 0x8E8843 opens the objectives screen; the host decides by the game's mode
	PalantirObjectives,
	TributeReturnToGame, // lane PLAY-1: PlayerTribute.apt's "<path>_ReturnToGame" (RW 0x914E89 -> RW 0x914C51: the screen closes, TheShell pops it)
	CreateAHeroExit      // lane CAH-1: AptCreateAHero::Class::Exit (the builder is left; the device ends the map mode)
};

struct ShellRequest
{
	ShellAction action = ShellAction::ExitGame;
	std::string argument; // the fscommand argument string
};

class ShellServices
{
public:
	virtual ~ShellServices() = default;
	// FSCommand:PlaySound (global command, BFME1 WindowManagerRegisterAptCallbacks0046FD40.cpp)
	virtual void playSound(const std::string &name) = 0;
	// FSCommand:SetBackground: "off", "fadein" or "fadeout" (spec 3.3 fixed commands)
	virtual void setBackground(const std::string &mode) = 0;
	// FSCommand:MouseSetVisibility
	virtual void setMouseVisible(bool visible) = 0;
	// The cursor tooltip chosen by the hover logic of WindowManager::update (0x0046E850): `label` is a game-text label
	// ("TOOLTIP:<name>"), or empty to clear it.
	virtual void setCursorTooltip(const std::string &label) = 0;
	virtual void request(const ShellRequest &request) = 0;
	// lane UI-2: an option the Options screen saved takes effect (key and value as Options.ini holds them, e.g. "SoftParticles" = "no")
	virtual void applyOption(const std::string &key, const std::string &value) = 0;
};

// Records every call, for tests and for hosts that have not wired a service yet (the recorded list is the report).
class RecordingShellServices : public ShellServices
{
public:
	std::vector<std::string> sounds;
	std::vector<std::string> backgrounds;
	std::vector<bool> mouseVisibility;
	std::vector<std::string> tooltips;
	std::vector<ShellRequest> requests;

	void playSound(const std::string &name) override { sounds.push_back(name); }
	void setBackground(const std::string &mode) override { backgrounds.push_back(mode); }
	void setMouseVisible(bool visible) override { mouseVisibility.push_back(visible); }
	void setCursorTooltip(const std::string &label) override { tooltips.push_back(label); }
	void request(const ShellRequest &r) override { requests.push_back(r); }
	std::vector<std::pair<std::string, std::string>> appliedOptions; // lane UI-2
	void applyOption(const std::string &key, const std::string &value) override { appliedOptions.emplace_back(key, value); }

	std::size_t countRequests(ShellAction action) const
	{
		std::size_t n = 0;
		for (const ShellRequest &r : requests)
		{
			if (r.action == action)
			{
				++n;
			}
		}
		return n;
	}
};
