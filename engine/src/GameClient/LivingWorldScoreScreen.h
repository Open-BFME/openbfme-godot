// OpenBFME. GPL-3.0.
//
// The War of the Ring score screen's data (TimeLine.apt type 6, WOTR2 milestone 5; spec P): AptTimeLine's fill RW 0x9275EC for a war and
// CollectPlayerData RW 0x92723E per LivingWorldPlayer, from the logic's score keepers (LivingWorldLogic.h ScoreKeeper).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the rows: every Living World player, defeated ones too, the local player first, the others in setup order; each row's status 2 left (P + 0x445),
//     1 defeated (+ 0x444), 3 still at war (RW 0x6E2378), else 0 won; the colour the player's colour | 0xFF000000, the side from the PlayerTemplate
//     the Living World template's Faction names (its + 0x1BC the evil flag);
//   * the local row with status 0 / 1 sets the win / loss page: APT:EndVictorious / APT:EndDefeat, the scenario's DisplayName, the condition text
//     (RW 0x901502 / 0x901568: UseMpRulesVictoryCondition -> the victory type's DisplayVictoriousText / DisplayDefeatedText, else the scenario's),
//     the image StrategicEndGame{Evil|Good}{Win|Lose};
//   * the statistics (RW 0x9CD79A): the turns played (the defeat turn, else the current turn, + 1), the score keeper's counters; the graph samples
//     its PerTurnStats, the marks its fortress and region-unified turns.
// NOT PORTED [S-1331]: the real-time battles' statistics (rows 0, 3, 4, 6, 16 and their parts of 12 / 13: the tactical ScoreKeeper merge RW 0x8F33A0)
// and the left flag (P + 0x445: its writer is not found).

#pragma once

#include "GameClient/EndGame.h"

#include <functional>
#include <string>
#include <vector>

namespace LivingWorld
{
class LivingWorldLogic;
}

struct WarScoreScreenInput
{
	int localPlayer = -1;
	std::vector<std::uint32_t> colors;       // per player: the colour RGB
	std::vector<std::string> sides;          // per player: the tactical PlayerTemplate's side
	std::vector<bool> evil;                  // per player: its evil flag
	std::uint32_t strategicSeconds = 0;      // the local wall clock outside battles (the score keeper's + 0x6C, the host's)
	float multipliers[3] = { 1.0f, 1.0f, 1.0f }; // GlobalData ScoreKeeper_UnitsBuilt / StructuresBuilt / RegionResources multipliers
};

ScoreScreenData buildWarScoreScreen(const LivingWorld::LivingWorldLogic &logic, const WarScoreScreenInput &in);
