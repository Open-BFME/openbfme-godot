// OpenBFME. GPL-3.0.
//
// TributeCommands (lane PLAY-1): the tribute, MSG_GIVE_MONEY (1126), the money a player sends an ally from PlayerTribute.apt (the Palantir's flag).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; donor: the BFME2 decomp GameLogic/System/Rva0023D793Transfer.cpp GameLogic::CallPlayerGiveMoney, byte-matched,
// tier B same-shape for RW 0x6264E1):
//   * the screen's Send (RW 0x914DD6): for every row whose player is not the local one, is active (RW 0x6AAC52) and whose slider holds an amount > 0, one
//     MSG_GIVE_MONEY (0x466) with three integers: the local player's index (Player + 0x54), the row player's index, the amount;
//   * the command dispatcher's case 0x466 (RW 0x779A3D -> RW 0x77BB..) passes the three to CallPlayerGiveMoney (RW 0x6264E1):
//       - refused before the logic frame reaches NumMinutesBeforePlayersCanTransferMoney * LOGICFRAMES_PER_SECOND * 60 (RW 0x626087, unsigned compare);
//       - both indices in [0, 20); the players are found by index among ThePlayerList's first 20 (RW 0x6A844E);
//       - refused when either player's byte + 0x754 is set (RW 0x6AAC4B);
//       - the amount is min(the giver's cash (Player + 0x94), the request); Money::withdraw(amount, giver's ScoreKeeper (+ 0x3DC), sound) (RW 0x7B17EF) gives
//         what was taken, Money::deposit(taken, receiver's ScoreKeeper, sound) (RW 0x7B18B8); then the giver's ScoreKeeper + 0x10 += taken (RW 0x79DD01) and
//         the receiver's + 0xC += taken (RW 0x79DCE9).
//   * RW 0x6264E1 does not compare the message's player with the giver, nor test that the two are allies: neither does this port.
// INFERENCE (stop S-1922): Player + 0x754 is taken as the port's Player::isDefeated (which also covers + 0x35A).
#pragma once

class GameLogic;
class GameLogicDispatch;
class GameMessage;

namespace TributeCommands
{
void registerHandlers(GameLogicDispatch &dispatch);
// RW 0x6264E1; false only for a malformed message (counted by the dispatcher)
bool giveMoney(GameLogic &logic, const GameMessage &msg);
// RW 0x6264E1 with the arguments; true when money moved
bool callPlayerGiveMoney(GameLogic &logic, int fromIndex, int toIndex, unsigned amount);
} // namespace TributeCommands
