// OpenBFME. GPL-3.0.
//
// QuitMenu.apt (CodePrefix AptQuitMenu, lane END-2): the in-game menu Esc and the Palantir's options button open over a live game.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the class: ctor RW 0x9222B2, dtor RW 0x921904, update RW 0x9226AA, the instance pointer RW 0xDEA378):
//   * opening (RW 0x921B0F, ToggleQuitMenu RW 0x921C9D: an open menu is closed with code 0 instead): only without a menu and outside the various
//     blocking states; the in-game panels close, a non-multiplayer game pauses (RW 0x625AF1(1, 0, 1)), then QuitMenu.apt is pushed (RW 0x75E3B7).
//   * the ctor registers the commands AptQuitMenu::OnInitialized (RW 0x921D5B), ::RestartMission (RW 0x92175F: + 0x27E = 1), ::ExitMission (RW 0x921769:
//     + 0x27C = 1, the options screen closed (RW 0x917A47), close with code 2), ::OptionsScreen (RW 0x921783 -> RW 0x91ED91(0, 0, 0, 0)), ::ReturnToGame
//     (RW 0x921794: close with code 0), ::SaveMenu / ::LoadMenu (RW 0x92179F / 0x9217F0: the save / load screen; lane SAVE-2: posted as QuitMenuSave /
//     QuitMenuLoad with the game type, the host opens SaveLoad.apt over the game); the tooltip
//     QuitMenu/Restart/TheButton (RW 0x921CB4: TOOLTIP:QuitMenu/Restart/TheButton with no game or a game of kind 3, the Living World's
//     TOOLTIP:QuitMenu/Surrender/WOTRSurrender, else TOOLTIP:QuitMenu/Forfeit/WOTRForfeit); the providers of RW 0xC7DB28 (RW 0x9216EC): HasFocus = "1",
//     AptQuitMenu::RestartPopupType = "Restart" (no game or kind 3), "Surrender" (Living World), else "Forfeit". In a multiplayer game the text record
//     APT:Pause becomes GUI:Menu; the dtor sets it back to APT:Pause.
//   * OnInitialized (RW 0x921D5B; the movie's disableButton(name)): a multiplayer game or a replay disables Restart when the kind is 3 or the local
//     player is defeated (Player + 0x754, RW 0x6AAC4B), then Save and Load (the Living World branches aside); a non-multiplayer game disables nothing.
//     The text APT:RestartOrSurrender = APT:Restart (no game or kind 3), APT:Surrender (Living World), else APT:Forfeit.
//   * the update (RW 0x9226AA) runs a pending RestartMission: a non-multiplayer game of kind 3 restarts (RW 0x9220DE: close, clearGameData, the same
//     MSG_NEW_GAME again); otherwise RW 0x921841: close with code 0, then unless TheVictoryConditions vslot 0x48 (the local alliance won)
//     MSG_SELF_DESTRUCT(false) (the logic's VictoryConditions::selfDestruct).
//   * the dtor (RW 0x921904) after an Exit: a multiplayer game before logic frame 6 (RW 0xBFD298) quits through TheNetwork (vslot 0x94); else (kind 3 or no
//     campaign object) RW 0x625E36(1): a multiplayer game whose TheGameInfo vslot 0x50 says no sends MSG_SELF_DESTRUCT(true), then MSG_CLEAR_GAME_DATA
//     (RW 0x779E28: clearGameData with the score screen for a skirmish or a multiplayer game). Without an Exit a non-multiplayer game is unpaused.
//   * lane SAVE-UI1: the backgrounds: the ctor ends with RW 0x6230B6(2) (ShowInGameBackground, RW 0x92262A); the dtor shows the front-end background after
//     an Exit (+ 0x27C, RW 0x921A7C), else hides the background (RW 0x622C88(0), RW 0x921A96) unless + 0x27D is set (RW 0x9226DF: a skirmish's restart;
//     SaveLoad's load over the game, RW 0x8183D0, is the host's: the game is cleared behind the load screen).
// The screen posts its decisions as ShellRequests (QuitMenuReturn / Exit / Restart / Forfeit / Options); the host closes the movie and acts.
// NOT PORTED (stop S-1064): the Living World branches, the network quit of the first six frames (the host's Exit leaves through
// the same path at any frame), the asset transfer of MSG_SELF_DESTRUCT(true) to a living ally.

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h"

#include <memory>
#include <string>
#include <vector>

struct ShellEnvironment;

class AptQuitMenu : public AptScreen
{
public:
	AptQuitMenu(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	~AptQuitMenu() override;

	// RW 0x921D5B, also run by the host tests
	void onInitialized();
	// RW 0x9216EC (AptQuitMenu::RestartPopupType) and RW 0x921CB4 (the tooltip label)
	std::string restartPopupType() const;
	std::string restartTooltip() const;
	// lane SAVE-2: the game type SaveMenu / LoadMenu open SaveLoad.apt with (RW 0x92179F: "2" Skirmish, "1" Campaign, "4" multiplayer)
	std::string saveLoadType() const;
	// lane SAVE-UI1 (OPTS-6): a screen opened over the menu (Options, SaveLoad) shuts it down the retail way, its movie unloaded and loaded again by the pop's
	// init (AptScreen.h); the C++ menu, its disabled buttons' source (the context) and its pending requests stay
	bool unloadsMovieWhenCovered() const override { return true; }
	const std::vector<std::string> &disabledButtons() const { return m_disabled; }
	const std::vector<std::string> &requests() const { return m_requests; }

	// ---- lane CHEAT-1: the open Cheat Mode's panel (NOT RETAIL; GameLogic/CheatMode.h) ----
	// The host of a Cheat Mode game gets a CHEAT MODE button in the menu (the FRIENDS button's look: the frame and lettering of Options.apt's page), under
	// the menu's own buttons; it opens an OpenBFME page (OpenBFMEOptionsPage: RotWK's combobox.wnd gadgets, Options.apt's frames and Albertus lettering)
	// over the menu. The device describes the page's rows and acts on its events (godot/scripts/cheat_panel.gd); the menu only hosts it.
	static constexpr int kCheatButtonLeft = 412, kCheatButtonTop = 604, kCheatButtonRight = 612, kCheatButtonBottom = 636; // stage pixels (1024 x 768)
	void setCheatButton(const UnicodeString &label);
	const UnicodeString &cheatButton() const { return m_cheatLabel; }
	bool openCheatPage(const OpenBFMEPageSpec &spec, std::string *error);
	void closeCheatPage();
	bool cheatPageOpen() const { return m_cheatPage && m_cheatPage->shown(); }
	OpenBFMEOptionsPage *cheatPage() const { return m_cheatPage.get(); }
	std::vector<OpenBFMEPageEvent> takeCheatEvents();
	void runInit() override;
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	void takeHooks(); // the mouse filter and the decorator (again after a screen that covered the menu took and cleared them)
	bool cheatMouse(int kind, float x, float y);
	void decorate(GadgetDrawList &out);
	void post(int action, const std::string &name, const std::string &argument);

	ShellEnvironment &m_env;
	std::vector<std::string> m_disabled;
	std::vector<std::string> m_requests;
	bool m_exiting = false;        // + 0x27C (lane SAVE-UI1)
	bool m_keepBackground = false; // + 0x27D (lane SAVE-UI1)
	// lane CHEAT-1
	UnicodeString m_cheatLabel;
	bool m_cheatHot = false, m_cheatPressed = false;
	std::unique_ptr<OpenBFMEOptionsPage> m_cheatPage;
	std::vector<OpenBFMEPageEvent> m_cheatEvents;
};
