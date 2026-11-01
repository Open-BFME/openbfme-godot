// OpenBFME. GPL-3.0.
//
// ScoreScreen.apt (CodePrefix AptScoreScreen): BFME1's score screen. BFME1 has no TimeLine.apt; _bfme_showScoreScreen (Open-BFME-1
// GUICallbacks/Apt/AptScoreScreen.cpp) pushes ScoreScreen.apt after a skirmish / multiplayer game and fills it with
// _bfme_populateMultiPlayer (VA 0x005770E0). The data contract is the shared GameClient/EndGame.h ScoreScreenData.
//
// DONOR FACTS (Open-BFME-1, BFME1 1.03 VAs):
//   * commands (AptScoreScreenConstructor.cpp): AptScoreScreen::OnInitialized, ::Exit, ::Save, ::Continue, ::RestartGame, ::RenameAccept, ::RenameCancel.
//   * providers (VA 0x005731E0, names VA 0x012B7F9C): NumOfPlayers (the rows), ShowSaveReplay (type 2 or 3), ShowCampaign (type 0), ScoreGrandTotal,
//     heroVetCount, unitsVetCount, PlayerFaction (single player), numberOfObjectives, ShowVictory ("_totalvictory" / "_victory" / "_survived"),
//     objectivesPerScore, NumberFormatter (setting keeps the number, reading formats it). Every one answers "0" first.
//   * ScoreScreen:PlayerColor:%d ("%d" of the row's multiplayer colour, VA 0x00571DF0), ScoreScreen:PlayerFaction:%d (the row's side, VA 0x00571E50),
//     Separator%d (rows 1..7), ScoreBattleStat / ScoreBonusObjectives / ScoreHeroVeterancy / ScoreUnitVeterancy / ScoreTerritoryBonus ("%d" of the
//     campaign scores, 0 in multiplayer), battleStat[A-G][1-3], objectiveChecked1..8, heroVetUpgrade1..12.
//   * the rows (VA 0x00576C20): the texts PlayerTable:<row>:0..7 = the player's name, units built, lost, destroyed, buildings built, lost, destroyed,
//     resources earned; the entries sorted by their team's score (VA 0x005770E0); a team break before a row whose player is not mutually allied with
//     the one before it.
// INFERENCE: ScoreScreenData keeps no alliances, so every row's team score is its own and the team break is set on every row; the sort comparator
// (BfmeScoreEntryLess) was not read: a higher score first. Continue and Exit leave as the RotWK score screen does (ShellAction::ScoreScreenContinue);
// the Separator%d body (VA 0x00570260) was not read: it answers the team break. NOT PORTED: the campaign page, Save Replay, Restart, unit renaming.

#pragma once

#include "GameClient/EndGame.h"
#include "GameClient/GUI/AptScreen.h"

#include <array>
#include <string>

struct ShellEnvironment;

class AptScoreScreen : public AptScreen
{
public:
	AptScoreScreen(WindowManager &windows, Shell &shell, ShellEnvironment &environment);

	void runInit() override;
	int continuePressed() const { return m_continue; }

private:
	const ScoreScreenData *data() const;
	void populate();

	ShellEnvironment &m_env;
	std::vector<int> m_rows;            ///< the entries' indices in the shown order
	std::array<bool, 8> m_teamBreak{};  ///< + 0x270
	std::string m_number;               ///< + 0x320: NumberFormatter's number
	int m_continue = 0;
	bool m_populated = false;
};
