// OpenBFME. GPL-3.0.
//
// Lane BFME2MODE-0 (AGENTS.md, docs/EDITIONS.md): the skirmish AI's edition variant. The one place the skirmish AI's rules differ between
// BFME2 1.06 and RotWK 2.01; each row cites both binaries. GameLogic::edition() picks the row (the mounted data decides it).
//
// Phase 0 ports one difference, the census's constant divergence in AIFarmKillSquad::initializeTeamTemplate (Open-BFME-2
// Code/GameEngine/Source/GameLogic/SkirmishAI/AITacticalAI/AITacticsGenerator/TargetlessTactics/AIFarmKillSquad.cpp:310, tier B same-shape):
//   RW    0x9BB479, at 0x9BB49D: mov dword ptr [esi + 0x21C], 500   (the team template's priority)
//   BFME2 0x9ACFE7, at 0x9AD00B: mov dword ptr [esi + 0x21C], 30
// both checked against the game.dat files by tools/retail_oracle/test_edition_oracle.py (the retail instructions run as a shim). The same function's
// forbidden KindOf mask differs too (RW 0x9BB499 `or byte [+0x2FD], 0x48`, BFME2 0x9AD007 `or byte [+0x2FD], 0x08`: RotWK adds the bit the port reads
// as ARCHER); it is not ported here (BFME2's KindOf numbering is unverified: Phase 1).
#pragma once

#include "Common/GameEdition.h"

namespace SkirmishAIEditionRules
{
constexpr int kFarmKillPriorityRotWK = 500; // RW 0x9BB49D
constexpr int kFarmKillPriorityBfme2 = 30;  // BFME2 0x9AD00B

// the FarmKillSquad team template's priority (+ 0x21C)
constexpr int farmKillPriority(GameEdition e)
{
	return e == GameEdition::BFME2_106 ? kFarmKillPriorityBfme2 : kFarmKillPriorityRotWK;
}
} // namespace SkirmishAIEditionRules
