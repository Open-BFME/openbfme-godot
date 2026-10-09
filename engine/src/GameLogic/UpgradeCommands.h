// OpenBFME. GPL-3.0.
//
// The upgrade research messages (lane UPGRADE-1) as GameLogicDispatch handlers. TARGET FACTS (RotWK game.dat, caveat S-001), the dispatcher cases at
// RW 0x77A6FD / 0x77A731:
//   * MSG_QUEUE_UPGRADE (1045): argument 1 is the upgrade's mask bit (RW 0x710C9E, TheUpgradeCenter->findUpgradeByMaskBit RW 0x66F218; an unknown bit does
//     nothing); the player's selection group (a missing group is RW's error path 0x77D076) runs RW 0x76FBFB: for every member, when
//     UpgradeCenter::canAffordUpgrade(member's controlling player, upgrade, member) (RW 0x66F492) and, for an OBJECT upgrade, the member does not have it
//     (RW 0x691421) and is affected by it (RW 0x694914), and the member has a ProductionUpdate (RW 0x68C327) whose canQueueUpgrade is not 4 (slot 1):
//     queueUpgrade (slot 3);
//   * MSG_CANCEL_UPGRADE (1046): argument 0 is the mask bit; every member of the group with a ProductionUpdate runs cancelUpgrade (slot 4, RW 0x76FC86).
// Argument 0 of MSG_QUEUE_UPGRADE is not read by RW's case (the control bar writes the producer's id there).

#pragma once

class GameLogic;
class GameLogicDispatch;
class GameMessage;

namespace UpgradeCommands
{
void registerHandlers(GameLogicDispatch &dispatcher);
bool queueUpgrade(GameLogic &logic, const GameMessage &m);
bool cancelUpgrade(GameLogic &logic, const GameMessage &m);
} // namespace UpgradeCommands
