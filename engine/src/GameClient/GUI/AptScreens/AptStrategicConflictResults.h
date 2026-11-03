// OpenBFME. GPL-3.0.
//
// StrategicConflictResults.apt (CodePrefix AptStrategicConflictResults, WOTR2 milestone 5): the War of the Ring's "Battle Statistics" screen after an
// AUTO-RESOLVED battle (spec N; the resolver's ConflictResultsState RW 0x9DF12F). No win / lose title, no timer: it stays until CONTINUE.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the Impl ctor RW 0x9FE832 (BFME2 0x5EB9F2): the commands AptStrategicConflictResults::OnInitialized (RW 0x9FEF35: state 1 -> 2), ::OnFadeOut
//     (RW 0x9FEF45: state 4), ::Continue (RW 0x9FEF4F: state 5); the externs Player_%d_Color (d 0..5, RW 0x9FE795: "%d" of the row's player colour
//     | 0xFF000000, "0" for a row with no player), StrategicConflicResults::NumWinners / ::NumPlayers (retail spelling, RW 0x9FE5BD: "%d");
//   * SetCurrentBattle RW 0x9FEBFF with the row order RW 0x9FEB0E (the winning side's players first, then the other sides in order): the texts
//     APT:Player_<row>_Name_String (the player's name, "Unknown" without one), _Destoyed_String (retail spelling), _HeroLost_String, _Lost_String,
//     _Veterancy_String ("%d" each) and the image StrategicConflictResults::PlayerFaction_<row> = the player's LivingWorldPlayerTemplate FactionIcon;
//   * Show RW 0x9FEE57: fill, then state 0 -> ShowLevel (state 1), 3 / 4 -> SetState("_fadeIn") (state 2); the update RW 0x9FE6CF: state 4 ->
//     HideLevel (state 0), state 5 -> the fade (RW 0x9FE665: SetState("_fadeOut"), state 3); active (RW 0x9FE6BA) = state not 0 and not 4.
// OpenBFME: the screen object lives while the level is shown (the host makes it at Show and drops it when inactive): the ctor's hidden load and
// ShowLevel collapse into the load, HideLevel into the destruction.

#pragma once

#include "GameClient/GUI/AptScreen.h"

#include <cstdint>
#include <string>
#include <vector>

struct ConflictResultsData
{
	struct Row
	{
		std::string name; // UTF-8 ("" -> "Unknown")
		int destroyed = 0, heroesLost = 0, lost = 0, veterancy = 0;
		std::string factionImage;
		std::uint32_t color = 0; // the player's colour RGB (player + 0x1B0)
	};
	std::vector<Row> rows; // row order
	int numWinners = 0;
};

class AptStrategicConflictResults : public AptScreen
{
public:
	AptStrategicConflictResults(WindowManager &windows, Shell &shell, const ConflictResultsData &data);

	void update();          // RW 0x9FE6CF, once per client update
	bool active() const { return m_state != 0 && m_state != 4; }
	int state() const { return m_state; }

private:
	void fill();
	ConflictResultsData m_data;
	int m_state = 1; // shown (ShowLevel), waiting for OnInitialized
};
