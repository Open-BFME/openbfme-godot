// OpenBFME. GPL-3.0.
//
// AptObjectivesMenu (lane CAMPUI-1): Objectives.apt, the mission objectives screen of a campaign game (the Palantir's flag, RW 0x6D40C9, and the Tab key
// DIPLOMACY, RW 0x81FFEB, open it in a game that is neither a skirmish nor a multiplayer game; there PlayerTribute.apt opens instead, RW 0x625456).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the opening RW 0x8E8843: nothing when one is open (RW 0xDE9EA8), the screen is disabled (DISABLE_OBJECTIVES_SCREEN), a menu is up (TheInGameUI vslot
//     0x17C) or the game ends (the script engine's end timer); else it pauses a game that is not a network game (mode 1 / 5) nor the tutorial (mode 6)
//     as the quit menu does (RW 0x625AF1(1, 0, 1)), closes the notification box (DoClose RW 0x8EB2D8) and pushes Objectives.apt (PlayerStatus.apt in a
//     skirmish / multiplayer game: + 0x288 = 1).
//   * the class (ctor RW 0x8E90DD, instance RW 0xDE9EA8, vtable RW 0xC78F40) registers the commands AptObjectivesMenu::OnInitialized (an empty body,
//     RW 0x8E90CC) and AptObjectivesMenu::ReturnToGame (RW 0x8E873E: closes, RW 0x8E86A5, unless the tutorial (mode 6) holds the input, InGameUI + 0x16),
//     the providers (RW 0x8E89E4, names RW 0xC78E88) NumOfPlayers ("0" here: the PlayerStatus count only), InSkirmish ("1" in a skirmish, mode 2, else
//     "0"), AptObjectivesMenu::InputEnabled ("0" only in the tutorial without InGameUI + 0x16, else "1"); and per row n = 1 .. 12 "Objective<n>" (RW
//     0x8E8BEB: "$" + the label of the n-th SHOWN objective (RW 0x8E8A8A), "" when there is none or the label is too long (2 + length >= 255)) and
//     "Objective<n>Status" (RW 0x8E8ADE: two characters, completed then bonus, '0' / '1'; "00" when there is no such row); ScoreScreen_PlayerColor_<n>
//     (the PlayerStatus page's) are not ported (not read by Objectives.apt); it ends with RW 0x6230B6(2), the in-game background.
//   * keys (RW 0x8E8B61): a key-down of Escape (1), Tab (0x0F) or Enter (0x1C) runs ReturnToGame (the host routes them).
//   * the dtor RW 0x8E8CED unpauses (RW 0x625AF1(0, 0, 1)) and hides the in-game background.
// The objectives are the game's TheMissionObjectiveTracker (GameLogic/ScriptEngine/MissionObjectiveTracker.h), copied in by the host when it opens the screen.

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameLogic/ScriptEngine/MissionObjectiveTracker.h"

#include <string>
#include <vector>

class ShellServices;
struct ShellEnvironment;

// what the screen shows: the host's copy of the game's tracker and the game mode (TheGameLogic + 0x110)
struct ObjectivesScreenInfo
{
	std::vector<MissionObjective> objectives;
	int gameMode = 0;            ///< 0 a campaign / single player game, 2 skirmish, 6 the tutorial
	bool tutorialInput = false;  ///< TheInGameUI + 0x16 (the tutorial lets the player close the screen)
};

class AptObjectivesMenu : public AptScreen
{
public:
	AptObjectivesMenu(WindowManager &windows, Shell &shell, ShellServices &services, ShellEnvironment &environment);
	~AptObjectivesMenu() override;
	static constexpr int kRows = 12;
	// RW 0x8E8BEB / 0x8E8ADE with the screen's data: row 0 .. 11
	static std::string objectiveText(const ObjectivesScreenInfo &info, int row);
	static std::string objectiveStatus(const ObjectivesScreenInfo &info, int row);
	// RW 0x8E873E: whether ReturnToGame closes the screen
	static bool returnAllowed(const ObjectivesScreenInfo &info) { return info.gameMode != 6 || info.tutorialInput; }

private:
	ShellServices &m_services;
	ShellEnvironment &m_env;
};
