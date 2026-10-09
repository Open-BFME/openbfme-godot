// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// MSG_SELF_DESTRUCT and Player::transferAssetsFromThat (lane MP-2): what happens to a player who leaves a network game mid-game. The disconnect path drops a
// silent player with DESTROYPLAYER (GameNetwork/DisconnectManager.h); every peer turns it into MSG_SELF_DESTRUCT { boolean TRUE } of that player in the
// same frame (RotWK RW 0x65E234), executed here.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * logicMessageDispatcher RW 0x779A3D, case MSG_SELF_DESTRUCT (1096; jump table RW 0x77D127 / index RW 0x77D1E3 -> RW 0x77CA3D): no player -> nothing;
//     argument 0 TRUE: the first other player (ThePlayerList order) that this player and that one both call ALLIES (Player::getRelationship of the other's
//     default team, RW 0x6ADBEB == 2, both ways) and that is not defeated (TheVictoryConditions vslot 0x40 = hasSinglePlayerBeenDefeated) gets the assets
//     (otherPlayer->transferAssetsFromThat(thisPlayer, TRUE), RW 0x6AF598) and the player is killed (Player::killPlayer RW 0x6ABE7C); no such ally: killed;
//     then TheGameLogic + 0x... RW 0x6328EE (it terminates helper processes: no logic effect). FALSE: killed, then a Living World branch (TheGameInfo + 0xB4).
//   * transferAssetsFromThat RW 0x6AF598 (this = the ally, `that` = the leaver, `inherited`): the leaver's objects (its teams' members, RW 0x7A4AB3) whose
//     template KindOf has none of INERT (tt + 0x108 bit 89), MOVE_ONLY (134), IGNORED_IN_GUI (47) are split by status TEMPORARILY_DEFECTED (62); with the
//     ally's score keeper's counting switched off (RW 0x79DC9E, Player + 0x3DC + 0x110) every other one: `inherited` -> status INHERITED_FROM_ALLY_TEAM (80)
//     (RW 0x62684D); a member of a HORDE container (object + 0x27C, container KindOf bit 109): the CONTAINER joins the ally's default team; else the
//     object's contain interface hands its occupants over (vslot 0x54, when vslot 0x10 or 8 answers) and the object joins the team (Object::setTeam RW
//     0x69954A) and two lists of RW 0xDE4AB8 follow it (RW 0x6D876A / 0x6D9042); the ally's ScoreKeeper addObjectBuilt(object, 1) (RW 0x79F0E1). The
//     counting switch is restored. The defected ones that are not HORDE_MEMBER (38) end their defection (RW 0x69ABA7). The leaver's completed upgrades
//     (Player + 0x14C, lowest bit first through TheUpgradeCenter RW 0x66F468) go to the ally (addUpgrade(upgrade, COMPLETE, not silent), RW 0x6AEE22)
//     except the fourteen names RW 0xC14034 .. 0xC14170 (the faction and dual-economy choice upgrades, Gandalf the White, Anduril, the Elven Gift: binary
//     constants, compared case sensitively). The leaver's whole cash moves to the ally (Money::withdraw / deposit with the sound, RW 0x7B17EF / 0x7B18B8).
//     The ally's command point territory counters (Player + 0x60 + 0x18 / + 0x1C) add the leaver's, plus one for the leaver's side (PlayerTemplate + 0x1BC
//     evil -> the evil counter).
// DONOR (ZH GameLogicDispatch MSG_SELF_DESTRUCT): the same ally search (ZH passes FALSE from processDestroyPlayerCommand; RotWK TRUE).
// NOT PORTED (stop S-1122): the contain interface's occupant hand-over (vslot 0x54: here the occupants change owner in the same pass as every other object
// of the leaver), the RW 0xDE4AB8 lists, the end of a defection (RW 0x69ABA7, as S-853), the Living World branch, RotWK's team / member order (here the
// object list order); a horde member whose container joined the ally keeps its own team in this engine unless moved too: it is moved after its container
// (INFERENCE from ZH OpenContain::onCapture, RotWK's horde capture not read).

#pragma once

class GameLogic;
class GameLogicDispatch;
class Player;

// registers the logic's executor of MSG_SELF_DESTRUCT (lane MP-2) unless another lane already registered one (END-2's surrender uses the same message)
void RegisterSelfDestructHandler(GameLogicDispatch &dispatch);

namespace SelfDestruct
{
// RW 0x77CA3D's body for `player` (TRUE: the assets go to a living ally)
void execute(GameLogic &logic, Player &player, bool transferAssets);
// RW 0x6AF598: `ally` takes `that`'s objects, upgrades, cash and territory counters
void transferAssetsFromThat(GameLogic &logic, Player &ally, Player &that, bool inherited);
// the counts of what the executor did (reports)
struct Stats
{
	unsigned long long executed = 0, transfers = 0, kills = 0, objectsTransferred = 0, upgradesTransferred = 0;
};
const Stats &stats();
} // namespace SelfDestruct
