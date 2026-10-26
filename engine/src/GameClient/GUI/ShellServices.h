// OpenBFME. GPL-3.0.
//
// What the shell and its screens ask of the rest of the engine (menus-apt.md 3.3, step A4).  The WindowManager, Shell and the
// Apt screens are headless engine code; every effect outside them (audio, the application quit, starting a campaign, the
// new-game message of Skirmish) goes through this interface, so the tests record it and the devices implement it.
//
// A request is a fact the UI produced ("the player chose Exit"), not a behaviour: the engine behind it is another lane's.

#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <utility>
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
	ObjectivesReturnToGame, // lane CAMPUI-1: AptObjectivesMenu::ReturnToGame (RW 0x8E873E -> RW 0x8E86A5: the screen closes, TheShell pops it, the game goes on)
	TributeReturnToGame, // lane PLAY-1: PlayerTribute.apt's "<path>_ReturnToGame" (RW 0x914E89 -> RW 0x914C51: the screen closes, TheShell pops it)
	CreateAHeroExit,     // lane CAH-1: AptCreateAHero::Class::Exit (the builder is left; the device ends the map mode)
	OnlineGameStart,     // lane ONLINE-4: OnlineShell.apt's staging room has the lobby's START (the argument: its JSON, GameNetwork/OnlineStart.h)
	// lane SAVE-2: SaveLoad.apt's saved-game pages (AptSaveLoad, see AptSaveLoad.h) and the quit menu's SaveMenu / LoadMenu
	SaveGameFile,        // AptSaveLoad::Save (RW 0x817B5F -> RW 0x817D0E's save): the argument is the name typed (UTF-8); the host saves and answers saveFinished
	LoadGameFile,        // AptSaveLoad::Load of a saved game (RW 0x816DBF, state 3): the argument is the file
	DeleteGameFile,      // AptSaveLoad::Delete confirmed (RW 0x81680D, state 0x14 -> RW 0x81707F): the argument is the file; the host deletes it and lists again
	QuitMenuSave,        // AptQuitMenu::SaveMenu (RW 0x92179F -> RW 0x816655(3, kind, 1)): the argument is the game type (1 Campaign, 2 Skirmish)
	QuitMenuLoad,        // AptQuitMenu::LoadMenu (RW 0x9217F0 -> RW 0x816655(2, kind, 1))
	OpenProjectWebsite,  // user-initiated, fixed OpenBFME website (no movie-provided URL)
	// lane CAMPSAVE-1: CampaignMenu.apt (AptCampaignMenu, see AptCampaignMenu.h); the host acts on them
	CampaignMenuMainMenu,    // OnBttnMainMenu (RW 0x927A88): the menu closes, the main menu
	CampaignMenuSave,        // OnBttnSaveGame (RW 0x927A93 -> RW 0x816655(3, 1, 0))
	CampaignMenuLoad,        // OnBttnLoadGame (RW 0x927AA4 -> RW 0x816655(2, 1, 0))
	CampaignMenuLastMission, // OnBttnLastMission (RW 0x927AB5 -> RW 0x5EC5D1)
	CampaignMenuNextMission  // OnBttnNextMission (RW 0x927ACF: message 0x21 -> RW 0x5EAE37)
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
	// lane OPTS-4: the Options screen's volume samples (RW 0x91F90D). TheAudio->addAudioEvent of the MiscAudio slot `field` ("VolumeSampleMusic" ...)
	// built with the view type `viewType` (the event's + 0x30, AudioEventRTS.h: RW 0x91F90D passes 2): the handle, 0 when the slot is NoSound or
	// nothing could be asked (the host reports why)
	virtual std::uint32_t addMiscAudioEvent(const std::string &field, int viewType) = 0;
	// TheAudio->removeAudioEvent / isCurrentlyPlaying
	virtual void removeAudioEvent(std::uint32_t handle) = 0;
	virtual bool isCurrentlyPlaying(std::uint32_t handle) = 0;
	// lane OPTS-5: TheDisplay vslot 0x64 setGamma(gamma, 0, 1, false): the Options screen's brightness slider while it moves (RW 0x91F90D), its Reset
	// (RW 0x91F5F3) and Cancel (RW 0x91ECB8, the saved GlobalData + 0xBCC) set the display's gamma at once, before anything is saved
	virtual void setDisplayGamma(float gamma) = 0;
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
	// lane OPTS-4: the samples asked for ({ handle, field }, handles from 1), the handles removed, and the ones still sounding (a test ends a sample by
	// erasing its handle from `sounding`, as the sound's end does)
	std::vector<std::pair<std::uint32_t, std::string>> miscEvents;
	std::vector<int> miscEventViews; // the view type of each (miscEvents' order)
	std::vector<std::uint32_t> removedEvents;
	std::set<std::uint32_t> sounding;
	std::uint32_t addMiscAudioEvent(const std::string &field, int viewType) override
	{
		const std::uint32_t handle = (std::uint32_t)miscEvents.size() + 1;
		miscEvents.emplace_back(handle, field);
		miscEventViews.push_back(viewType);
		sounding.insert(handle);
		return handle;
	}
	void removeAudioEvent(std::uint32_t handle) override
	{
		removedEvents.push_back(handle);
		sounding.erase(handle);
	}
	bool isCurrentlyPlaying(std::uint32_t handle) override { return sounding.count(handle) != 0; }
	std::vector<float> gammas; // lane OPTS-5: every display gamma set
	void setDisplayGamma(float gamma) override { gammas.push_back(gamma); }

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
